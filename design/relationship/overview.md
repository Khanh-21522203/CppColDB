Entry:
  [main CLI loop] <--stdin-- [Terminal]
  [main CLI loop] --submits SQL to--> [Connection]
  [main CLI loop] --prints query output to--> [stdout]
Application:
  [Database] --creates sessions via--> [Connection]
  [Connection] --configures query context for--> [ClientContext]
  [Connection] --dispatches SQL pipeline via--> [ClientContext]
  [Connection] --begins/commits/rolls back via--> [TransactionManager]
  [Database] --initializes catalog via--> [Catalog]
  [Database] --initializes transaction control via--> [TransactionManager]
  [Database] --initializes background runtime via--> [TaskScheduler]
  [Database] --runs shutdown checkpoint via--> [CheckpointManager]
  [Database] --restores catalog from--> [BlockFile]
  [Database] --replays startup log from--> [WAL]
  [Database] --applies replayed DDL/DML via--> [Catalog]
  [TaskScheduler] --dispatches queued tasks to--> [loop:TaskSchedulerWorker]
  [Connection] --returns query result to--> [main CLI loop]
Domain:
  [ClientContext] --tokenizes SQL with--> [Tokenizer]
  [ClientContext] --parses tokens with--> [Parser]
  [ClientContext] --binds statement with--> [Binder]
  [ClientContext] --optimizes logical plan with--> [Optimizer]
  [ClientContext] --builds physical plan with--> [PhysicalPlanner]
  [ClientContext] --executes plan with--> [Executor]
  [Binder] --reads table metadata from--> [Catalog]
  [Executor] --runs pipelines through--> [PipelineExecutor]
  [Executor] --collects rows in--> [PhysicalResultCollector]
  [PipelineExecutor] --pulls rows from--> [PhysicalTableScan]
  [PipelineExecutor] --drives DML/DDL operators--> [PhysicalInsert]
  [PipelineExecutor] --drives DML/DDL operators--> [PhysicalUpdate]
  [PipelineExecutor] --drives DML/DDL operators--> [PhysicalDelete]
  [PipelineExecutor] --drives DML/DDL operators--> [PhysicalCreateTable]
  [PipelineExecutor] --drives DML/DDL operators--> [PhysicalDropTable]
  [PipelineExecutor] --drives DML/DDL operators--> [PhysicalAlterTable]
  [PipelineExecutor] --drives two-phase operators--> [PhysicalSort]
  [PipelineExecutor] --drives two-phase operators--> [PhysicalSortSource]
  [PipelineExecutor] --drives two-phase operators--> [PhysicalHashAggregation]
  [PipelineExecutor] --drives two-phase operators--> [PhysicalAggregationSource]
  [PipelineExecutor] --drives two-phase operators--> [PhysicalHashJoinBuild]
  [PipelineExecutor] --drives two-phase operators--> [PhysicalHashJoinProbe]
  [PhysicalTableScan] --reads table metadata from--> [Catalog]
  [PhysicalTableScan] --reads visible rows from--> [RowGroup]
  [PhysicalInsert] --reads target table metadata from--> [Catalog]
  [PhysicalInsert] --appends/updates rows in--> [RowGroup]
  [PhysicalInsert] --records undo entries in--> [Transaction]
  [PhysicalUpdate] --reads target table metadata from--> [Catalog]
  [PhysicalUpdate] --reads visible rows from--> [RowGroup]
  [PhysicalUpdate] --appends/updates rows in--> [RowGroup]
  [PhysicalUpdate] --marks MVCC rows in--> [VersionInfo]
  [PhysicalUpdate] --records undo entries in--> [Transaction]
  [PhysicalDelete] --reads target table metadata from--> [Catalog]
  [PhysicalDelete] --reads visible rows from--> [RowGroup]
  [PhysicalDelete] --marks MVCC rows in--> [VersionInfo]
  [PhysicalDelete] --records undo entries in--> [Transaction]
  [PhysicalCreateTable] --mutates catalog via--> [Catalog]
  [PhysicalCreateTable] --records undo entries in--> [Transaction]
  [PhysicalDropTable] --mutates catalog via--> [Catalog]
  [PhysicalDropTable] --records undo entries in--> [Transaction]
  [PhysicalAlterTable] --mutates partition metadata via--> [Catalog]
  [PhysicalAlterTable] --records undo entries in--> [Transaction]
  [PhysicalSort] --writes intermediate rows to--> [SortBuffer]
  [PhysicalSortSource] --reads intermediate rows from--> [SortBuffer]
  [PhysicalHashAggregation] --writes group states to--> [AggregateHashTable]
  [PhysicalAggregationSource] --reads group states from--> [AggregateHashTable]
  [PhysicalHashJoinBuild] --writes build-side rows to--> [JoinHashTable]
  [PhysicalHashJoinProbe] --reads build-side rows from--> [JoinHashTable]
  [RowGroup] --checks row visibility via--> [VersionInfo]
  [RowGroup] --reads/writes column data via--> [ColumnChunk]
  [ColumnChunk] --reads segment blocks via--> [BufferManager]
  [ColumnChunk] --allocates/writes segment blocks via--> [BufferManager]
  [TransactionManager] --creates and tracks--> [Transaction]
  [TransactionManager] --reads undo/redo intents from--> [Transaction]
  [TransactionManager] --applies catalog MVCC via--> [Catalog]
  [TransactionManager] --applies row-version commits via--> [VersionInfo]
  [Transaction] --writes redo entries to--> [WAL]
  [CheckpointManager] --flushes committed row groups via--> [Catalog]
  [CheckpointManager] --pins and flushes catalog block via--> [BufferManager]
  [CheckpointManager] --writes checkpoint marker to--> [WAL]
  [CheckpointManager] --truncates pre-checkpoint log in--> [WAL]
  [loop:TaskSchedulerWorker] --executes--> [AsyncCheckpointTask]
  [AsyncCheckpointTask] --calls--> [CheckpointManager]
  [Tokenizer] (SQL lexer)
  [Parser] (SQL parser)
  [Optimizer] (logical rewrite engine)
  [PhysicalPlanner] (physical plan builder)
  [PhysicalResultCollector] (returns result to caller)
  [SortBuffer] (two-phase sort materialization state)
  [AggregateHashTable] (two-phase aggregation materialization state)
  [JoinHashTable] (two-phase join build/probe materialization state)
  [Catalog] (shared metadata store)
  [VersionInfo] (shared MVCC marker store)
Infrastructure:
  [BufferManager] --reads/writes persistent blocks via--> [BlockFile]
  [WAL] --appends and replays log bytes via--> [Filesystem:wal file]
  [BlockFile] --stores data blocks via--> [Filesystem:db file]
External:
  [Terminal] (CLI input source)
  [stdout] (CLI output sink)
  [Filesystem:wal file] (durability log file)
  [Filesystem:db file] (persistent block store)
! violations: none
