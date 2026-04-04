+-- SQL Interface ----------------------------------------------------------------------+
| [Database] --creates sessions via--> [Connection]                                     |
| [Connection] --configures query context for--> [ClientContext]                        |
| [Connection] --dispatches SQL pipeline via--> [ClientContext]                         |
| [Connection] --returns query result to--> [main CLI loop]                             |
| [ClientContext] (query pipeline coordinator)                                           |
| [main CLI loop] (CLI entrypoint)                                                      |
+----------------------------------------------------------------------------------------+

+-- Planning ---------------------------------------------------------------------------+
| [Tokenizer] (SQL lexer; invoked by ClientContext)                                     |
| [Parser] (SQL parser; invoked by ClientContext)                                       |
| [Binder] (statement binder; invoked by ClientContext)                                 |
| [Optimizer] (logical rewrite engine; invoked by ClientContext)                        |
| [PhysicalPlanner] (physical plan builder; invoked by ClientContext)                   |
+----------------------------------------------------------------------------------------+

+-- Execution --------------------------------------------------------------------------+
| [Executor] --runs pipelines through--> [PipelineExecutor]                             |
| [Executor] --collects rows in--> [PhysicalResultCollector]                            |
| [PipelineExecutor] --pulls rows from--> [PhysicalTableScan]                           |
| [PipelineExecutor] --drives DML/DDL operators--> [PhysicalInsert]                     |
| [PipelineExecutor] --drives DML/DDL operators--> [PhysicalUpdate]                     |
| [PipelineExecutor] --drives DML/DDL operators--> [PhysicalDelete]                     |
| [PipelineExecutor] --drives DML/DDL operators--> [PhysicalCreateTable]                |
| [PipelineExecutor] --drives DML/DDL operators--> [PhysicalDropTable]                  |
| [PipelineExecutor] --drives DML/DDL operators--> [PhysicalAlterTable]                 |
| [PipelineExecutor] --drives two-phase operators--> [PhysicalSort]                     |
| [PipelineExecutor] --drives two-phase operators--> [PhysicalSortSource]               |
| [PipelineExecutor] --drives two-phase operators--> [PhysicalHashAggregation]          |
| [PipelineExecutor] --drives two-phase operators--> [PhysicalAggregationSource]        |
| [PipelineExecutor] --drives two-phase operators--> [PhysicalHashJoinBuild]            |
| [PipelineExecutor] --drives two-phase operators--> [PhysicalHashJoinProbe]            |
| [PhysicalTableScan] (scan source operator)                                            |
| [PhysicalInsert] (insert source/sink operator)                                        |
| [PhysicalUpdate] (update source operator)                                             |
| [PhysicalDelete] (delete source operator)                                             |
| [PhysicalCreateTable] (DDL source operator)                                           |
| [PhysicalDropTable] (DDL source operator)                                             |
| [PhysicalAlterTable] (DDL source operator)                                            |
| [PhysicalSort] --writes intermediate rows to--> [SortBuffer]                          |
| [PhysicalSortSource] --reads intermediate rows from--> [SortBuffer]                   |
| [PhysicalHashAggregation] --writes group states to--> [AggregateHashTable]            |
| [PhysicalAggregationSource] --reads group states from--> [AggregateHashTable]         |
| [PhysicalHashJoinBuild] --writes build-side rows to--> [JoinHashTable]                |
| [PhysicalHashJoinProbe] --reads build-side rows from--> [JoinHashTable]               |
| [SortBuffer] (two-phase sort materialization state)                                   |
| [AggregateHashTable] (two-phase aggregation materialization state)                    |
| [JoinHashTable] (two-phase join build/probe materialization state)                    |
| [PhysicalResultCollector] (returns result to caller)                                  |
+----------------------------------------------------------------------------------------+

+-- Storage and Durability -------------------------------------------------------------+
| [TaskScheduler] --dispatches queued tasks to--> [loop:TaskSchedulerWorker]            |
| [loop:TaskSchedulerWorker] --executes--> [AsyncCheckpointTask]                        |
| [AsyncCheckpointTask] --calls--> [CheckpointManager]                                  |
| [TransactionManager] --creates and tracks--> [Transaction]                            |
| [TransactionManager] --reads undo/redo intents from--> [Transaction]                  |
| [TransactionManager] --applies catalog MVCC via--> [Catalog]                          |
| [TransactionManager] --applies row-version commits via--> [VersionInfo]               |
| [RowGroup] --checks row visibility via--> [VersionInfo]                               |
| [RowGroup] --reads/writes column data via--> [ColumnChunk]                            |
| [ColumnChunk] --reads segment blocks via--> [BufferManager]                           |
| [ColumnChunk] --allocates/writes segment blocks via--> [BufferManager]                |
| [Transaction] --writes redo entries to--> [WAL]                                       |
| [CheckpointManager] --flushes committed row groups via--> [Catalog]                   |
| [CheckpointManager] --pins and flushes catalog block via--> [BufferManager]           |
| [CheckpointManager] --writes checkpoint marker to--> [WAL]                            |
| [CheckpointManager] --truncates pre-checkpoint log in--> [WAL]                        |
| [BufferManager] --reads/writes persistent blocks via--> [BlockFile]                   |
| [WAL] --appends and replays log bytes via--> [Filesystem:wal file]                    |
| [BlockFile] --stores data blocks via--> [Filesystem:db file]                          |
| [Catalog] (shared metadata store)                                                     |
| [VersionInfo] (shared MVCC marker store)                                              |
+----------------------------------------------------------------------------------------+

[SQL Interface] <--stdin-- [Terminal]
[SQL Interface] --prints query output to--> [stdout]
[SQL Interface] --tokenizes SQL with--> [Planning]
[SQL Interface] --parses tokens with--> [Planning]
[SQL Interface] --binds statement with--> [Planning]
[SQL Interface] --optimizes logical plan with--> [Planning]
[SQL Interface] --builds physical plan with--> [Planning]
[SQL Interface] --executes plan with--> [Execution]
[SQL Interface] --begins/commits/rolls back via--> [Storage and Durability]
[SQL Interface] --initializes catalog via--> [Storage and Durability]
[SQL Interface] --initializes transaction control via--> [Storage and Durability]
[SQL Interface] --initializes background runtime via--> [Storage and Durability]
[SQL Interface] --runs shutdown checkpoint via--> [Storage and Durability]
[SQL Interface] --restores catalog from--> [Storage and Durability]
[SQL Interface] --replays startup log from--> [Storage and Durability]
[SQL Interface] --applies replayed DDL/DML via--> [Storage and Durability]
[Planning] --reads table metadata from--> [Storage and Durability]
[Execution] --reads table metadata from--> [Storage and Durability]
[Execution] --reads visible rows from--> [Storage and Durability]
[Execution] --reads target table metadata from--> [Storage and Durability]
[Execution] --appends/updates rows in--> [Storage and Durability]
[Execution] --marks MVCC rows in--> [Storage and Durability]
[Execution] --records undo entries in--> [Storage and Durability]
[Execution] --mutates catalog via--> [Storage and Durability]
[Execution] --mutates partition metadata via--> [Storage and Durability]
