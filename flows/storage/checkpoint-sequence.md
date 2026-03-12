# Checkpoint Sequence Diagram

## Assumptions
- Shows the interaction between components during an automatic checkpoint triggered by a commit.
- The WAL lock is held separately from the transaction lock to allow concurrent reads.
- The checkpoint WAL captures any writes that happen during the checkpoint process.

## Code Files Referenced
- `src/transaction/duck_transaction_manager.cpp` — `CommitTransaction()` checkpoint decision
- `src/storage/storage_manager.cpp` — `WALStartCheckpoint()`, `WALFinishCheckpoint()`
- `src/storage/checkpoint_manager.cpp` — `CheckpointManager::CreateCheckpoint()`

```mermaid
sequenceDiagram
    participant TxMgr as DuckTransactionManager
    participant Tx as DuckTransaction
    participant StorMgr as StorageManager
    participant WAL as WriteAheadLog
    participant CkptMgr as CheckpointManager
    participant BlockMgr as SingleFileBlockManager
    participant Catalog

    TxMgr->>TxMgr: CanCheckpoint(transaction)
    TxMgr->>TxMgr: Try acquire checkpoint lock

    alt Cannot checkpoint
        TxMgr-->>TxMgr: Skip checkpoint
    else Can checkpoint
        TxMgr->>StorMgr: CreateCheckpoint(context, options)

        StorMgr->>StorMgr: WALStartCheckpoint()
        StorMgr->>TxMgr: GetNewCheckpointId()
        TxMgr-->>StorMgr: checkpoint_transaction_id

        StorMgr->>WAL: WriteCheckpoint(meta_block)
        StorMgr->>WAL: Flush()
        StorMgr->>WAL: Close main WAL

        StorMgr->>StorMgr: Create checkpoint WAL (.wal.checkpoint)

        StorMgr->>CkptMgr: CreateCheckpoint()
        CkptMgr->>Catalog: Scan all schemas
        loop For each table
            CkptMgr->>CkptMgr: Write table metadata
            CkptMgr->>BlockMgr: Write row groups to disk
            CkptMgr->>BlockMgr: Write column data
            CkptMgr->>BlockMgr: Write indexes
        end
        CkptMgr->>BlockMgr: Flush all blocks
        CkptMgr-->>StorMgr: Checkpoint data written

        StorMgr->>StorMgr: WALFinishCheckpoint()

        alt Checkpoint WAL was NOT written to
            StorMgr->>StorMgr: Remove main WAL file
            StorMgr->>WAL: Create fresh empty WAL
        else Checkpoint WAL WAS written to
            StorMgr->>StorMgr: Close checkpoint WAL
            StorMgr->>StorMgr: Move checkpoint WAL → main WAL
            StorMgr->>WAL: Re-open main WAL
        end

        StorMgr-->>TxMgr: Checkpoint complete
    end
```
