/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id$"
/*======
This file is part of PerconaFT.


Copyright (c) 2006, 2015, Percona and/or its affiliates. All rights reserved.

    PerconaFT is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License, version 2,
    as published by the Free Software Foundation.

    PerconaFT is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with PerconaFT.  If not, see <http://www.gnu.org/licenses/>.

----------------------------------------

    PerconaFT is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License, version 3,
    as published by the Free Software Foundation.

    PerconaFT is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with PerconaFT.  If not, see <http://www.gnu.org/licenses/>.
======= */

#ident "Copyright (c) 2006, 2015, Percona and/or its affiliates. All rights reserved."

#pragma once

#include <stdint.h>
#include <inttypes.h>

// PORT2: #include "ft/txn/txn_state.h"
// PORT: #include "ft/serialize/block_table.h"
// PORT: #include "ft/ft-status.h"
#include "util/omt.h"

typedef uint64_t TXNID;

typedef struct tokutxn *TOKUTXN;

#define TXNID_NONE_LIVING ((TXNID)0)
#define TXNID_NONE        ((TXNID)0)
#define TXNID_MAX         ((TXNID)-1)

typedef struct txnid_pair_s {
    TXNID parent_id64;
    TXNID child_id64;
} TXNID_PAIR;

static const TXNID_PAIR TXNID_PAIR_NONE = { .parent_id64 = TXNID_NONE, .child_id64 = TXNID_NONE };

// We include the child manager here beacuse it uses the TXNID / TOKUTXN types
// PORT: #include "ft/txn/txn_child_manager.h"

/* Log Sequence Number (LSN)
 * Make the LSN be a struct instead of an integer so that we get better type checking. */
typedef struct __toku_lsn { uint64_t lsn; } LSN;
static const LSN ZERO_LSN = { .lsn = 0 };
static const LSN MAX_LSN = { .lsn = UINT64_MAX };

//
// Types of snapshots that can be taken by a tokutxn
//  - TXN_SNAPSHOT_NONE: means that there is no snapshot. Reads do not use snapshot reads.
//                       used for SERIALIZABLE and READ UNCOMMITTED
//  - TXN_SNAPSHOT_ROOT: means that all tokutxns use their root transaction's snapshot
//                       used for REPEATABLE READ
//  - TXN_SNAPSHOT_CHILD: means that each child tokutxn creates its own snapshot
//                        used for READ COMMITTED
//

typedef enum __TXN_SNAPSHOT_TYPE { 
    TXN_SNAPSHOT_NONE=0,
    TXN_SNAPSHOT_ROOT=1,
    TXN_SNAPSHOT_CHILD=2,
    TXN_COPIES_SNAPSHOT=3
} TXN_SNAPSHOT_TYPE;

typedef toku::omt<struct tokutxn *> txn_omt_t;
typedef toku::omt<TXNID> xid_omt_t;
typedef toku::omt<struct referenced_xid_tuple, struct referenced_xid_tuple *> rx_omt_t;

inline bool txn_pair_is_none(TXNID_PAIR txnid) {
    return txnid.parent_id64 == TXNID_NONE && txnid.child_id64 == TXNID_NONE;
}

struct tokulogger;

struct txn_roll_info {
    // these are number of rollback nodes and rollback entries for this txn.
    //
    // the current rollback node below has sequence number num_rollback_nodes - 1
    // (because they are numbered 0...num-1). often, the current rollback is
    // already set to this block num, which means it exists and is available to
    // log some entries. if the current rollback is NONE and the number of
    // rollback nodes for this transaction is non-zero, then we will use
    // the number of rollback nodes to know which sequence number to assign
    // to a new one we create
    uint64_t num_rollback_nodes;
    uint64_t num_rollentries;
    uint64_t num_rollentries_processed;
    uint64_t rollentry_raw_count;  // the total count of every byte in the transaction and all its children.

    // spilled rollback nodes are rollback nodes that were gorged by this
    // transaction, retired, and saved in a list.
#if 0 // PORT
    // the spilled rollback head is the block number of the first rollback node
    // that makes up the rollback log chain
    BLOCKNUM spilled_rollback_head;

    // the spilled rollback is the block number of the last rollback node that
    // makes up the rollback log chain. 
    BLOCKNUM spilled_rollback_tail;

    // the current rollback node block number we may use. if this is ROLLBACK_NONE,
    // then we need to create one and set it here before using it.
    BLOCKNUM current_rollback; 
#endif    
};

struct tokutxn {
    // These don't change after create:

    TXNID_PAIR txnid;

    uint64_t snapshot_txnid64; // this is the lsn of the snapshot
    const TXN_SNAPSHOT_TYPE snapshot_type;
    const bool for_recovery;
    struct tokulogger *const logger;
    struct tokutxn *const parent;
    // The child txn is protected by the child_txn_manager lock
    // and by the user contract. The user contract states (and is
    // enforced at the ydb layer) that a child txn should not be created
    // while another child exists. The txn_child_manager will protect
    // other threads from trying to read this value while another
    // thread commits/aborts the child
    struct tokutxn *child;

    // statically allocated child manager, if this 
    // txn is a root txn, this manager will be used and set to 
    // child_manager for this transaction and all of its children
    // PORT: txn_child_manager child_manager_s;

    // child manager for this transaction, all of its children,
    // and all of its ancestors
    //PORT: txn_child_manager* child_manager;

    // These don't change but they're created in a way that's hard to make
    // strictly const.
    DB_TXN *container_db_txn; // reference to DB_TXN that contains this tokutxn
    xid_omt_t *live_root_txn_list; // the root txns live when the root ancestor (self if a root) started.
    struct XIDS_S *xids; // Represents the xid list

    struct tokutxn *snapshot_next;
    struct tokutxn *snapshot_prev;

    bool begin_was_logged;
    bool declared_read_only; // true if the txn was declared read only when began

    // These are not read until a commit, prepare, or abort starts, and
    // they're "monotonic" (only go false->true) during operation:
    bool do_fsync;
    bool force_fsync_on_commit;  //This transaction NEEDS an fsync once (if) it commits.  (commit means root txn)

    // Not used until commit, prepare, or abort starts:
    LSN do_fsync_lsn;
    TOKU_XA_XID xa_xid; // for prepared transactions
    TXN_PROGRESS_POLL_FUNCTION progress_poll_fun;
    void *progress_poll_fun_extra;

    // PORT: toku_mutex_t txn_lock;
    // Protected by the txn lock:
    toku::omt<struct ft*> open_fts; // a collection of the fts that we touched.  Indexed by filenum.
    struct txn_roll_info roll_info; // Info used to manage rollback entries

    // mutex that protects the transition of the state variable
    // the rest of the variables are used by the txn code and 
    // hot indexing to ensure that when hot indexing is processing a 
    // leafentry, a TOKUTXN cannot dissappear or change state out from
    // underneath it
    // PORT: toku_mutex_t state_lock;
    // PORT: toku_cond_t state_cond;
    // PORT2: TOKUTXN_STATE state;
    uint32_t num_pin; // number of threads (all hot indexes) that want this
                      // txn to not transition to commit or abort
    uint64_t client_id;
    void *client_extra;
    time_t start_time;
};
typedef struct tokutxn *TOKUTXN;


// PORT2: TOKUTXN_STATE toku_txn_get_state(struct tokutxn *txn);
// PORT: this part of header is not needed:

