
/* MyRocks header files */
#include "./ha_rocksdb.h"

namespace myrocks {

//////////////////////////////////////////////////////////////////////////////
// Locking iterator
//////////////////////////////////////////////////////////////////////////////

//
// LockingIterator is an iterator that locks the rows before returning, as well
// as scanned gaps between the rows.
//
//  Example:
//    lock_iter= trx->GetLockingIterator();
//    lock_iter->Seek('abc');
//    lock_iter->Valid()==true && lock_iter->key() == 'bcd';
//
//   After the above, the returned record 'bcd' is locked by transaction trx.
//   Also, the range between ['abc'..'bcd'] is empty and locked by trx.
//
//    lock_iter->Next();
//    lock_iter->Valid()==true && lock_iter->key() == 'efg'
//
//   Now, the range ['bcd'.. 'efg'] (bounds incluive) is also locked, and there are no
//   records between 'bcd'  and 'efg'.
//
class LockingIterator : public rocksdb::Iterator {

  rocksdb::ColumnFamilyHandle* cfh_;
  rocksdb::Transaction *txn_;
  rocksdb::Iterator *iter_;
  rocksdb::Status status_;

 public:
  LockingIterator(rocksdb::Iterator *iter, rocksdb::ColumnFamilyHandle *cfh,
                  rocksdb::Transaction *txn) :
    cfh_(cfh), txn_(txn), iter_(iter), status_(rocksdb::Status::InvalidArgument()) {}

  virtual bool Valid() const override { return status_.ok(); }

  // Note: MyRocks doesn't ever call these:
  virtual void SeekToFirst() override;
  virtual void SeekToLast() override;

  virtual void Seek(const rocksdb::Slice& target) override;

  // Position at the last key in the source that at or before target.
  // The iterator is Valid() after this call iff the source contains
  // an entry that comes at or before target.
  virtual void SeekForPrev(const rocksdb::Slice& target) override;

  virtual void Next() override;
  virtual void Prev() override;

  virtual rocksdb::Slice key() const override {
    assert(Valid());
    return iter_->key();
  }

  virtual rocksdb::Slice value() const override {
    assert(Valid());
    return iter_->value();
  }

  virtual rocksdb::Status status() const override {
    return status_;
  }

 private:

  template <bool forward> void Scan(const rocksdb::Slice& target, bool call_next) {
    if (!iter_->Valid()) {
      status_ = iter_->status();
      return;
    }

    while (1) {
      /*
        note: the underlying iterator checks iterator bounds, so we don't need
        to check them here
      */
      auto end_key = iter_->key();
      if (forward)
        status_ = txn_->GetRangeLock(cfh_, rocksdb::Endpoint(target), rocksdb::Endpoint(end_key));
      else
        status_ = txn_->GetRangeLock(cfh_, rocksdb::Endpoint(end_key), rocksdb::Endpoint(target));

      if (!status_.ok()) {
        // Failed to get a lock (most likely lock wait timeout)
        return;
      }

      //Ok, now we have a lock which is inhibiting modifications in the range
      // Somebody might have done external modifications, though:
      //  - removed the key we've found
      //  - added a key before that key.

      //TODO: refresh the iterator here? 
      if (forward)
        iter_->Seek(target);
      else
        iter_->SeekForPrev(target);

      if (call_next && iter_->Valid()) {
        if (forward)
          iter_->Next();
        else
          iter_->Prev();
      }

      if (iter_->Valid()) {
        int invert= forward? 1 : -1;
        if (cfh_->GetComparator()->Compare(iter_->key(), end_key) * invert <= 0) {
          // Ok, the key is within the range.
          status_ = rocksdb::Status::OK();
          break;
        } else {
          // We've got a row but it is outside the range we've locked.
          // Re-try the lock-and-read step.
          continue;
        }
      } else {
        // There's no row (within the iterator bounds perhaps). Exit now.
        // (we might already have locked a range in this function but there's
        // nothing we can do about it)
        status_ = iter_->status();
        break;
      }
    }
  }

  inline void ScanForward(const rocksdb::Slice& target, bool call_next) {
    Scan<true>(target, call_next);
  }

  inline void ScanBackward(const rocksdb::Slice& target, bool call_next) {
    Scan<false>(target, call_next);
  }
};

rocksdb::Iterator* GetLockingIterator(
    rocksdb::Transaction *trx,
    rocksdb::Iterator *base_iter,
    const rocksdb::ReadOptions& read_options,
    rocksdb::ColumnFamilyHandle* column_family);

} // namespace myrocks
