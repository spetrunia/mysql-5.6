--echo # select * from information_schema.rocksdb_locks; # With replacements by select_from_is_rowlocks.inc
--disable_query_log
set @cf_id=(select column_family from information_schema.rocksdb_ddl
            where table_name='t1' and index_name='PRIMARY');
set @rtrx_id=(select transaction_id from information_schema.rocksdb_trx
              where thread_id=connection_id());
set @indexnr= (select lpad(hex(index_number),6,'0') from information_schema.rocksdb_ddl
               where table_name='t1' and index_name='PRIMARY');

set @indexnr_next= (select lpad(hex(index_number+1),6,'0') from information_schema.rocksdb_ddl
                    where table_name='t1' and index_name='PRIMARY');

let $extra_where = where 1;

if ($select_from_is_rowlocks_current_trx_only) 
{
  let $extra_where = where transaction_id=(select transaction_id from information_schema.rocksdb_trx where connection_id()=thread_id);
}

eval select 
  replace(column_family_id, @cf_id, "\$cf_id")  as COLUMN_FAMILY_ID,
  replace(transaction_id, @rtrx_id, "\$trx_id") as TRANSACTION_ID,
  replace(
     replace(`key`, @indexnr, '\${indexnr}'),
     @indexnr_next, '\${indexnr+1}'
  ) as `KEY`,
  mode
from information_schema.rocksdb_locks $extra_where;

--enable_query_log
