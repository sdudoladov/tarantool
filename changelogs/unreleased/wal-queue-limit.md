## feature/core

* Introduce the concept of WAL queue and 2 new configuration options:
  `wal_queue_max_len`, measured in transactions, with 100k default and
  `wal_queue_max_size`, measured in bytes, with 100 Mb default.
  The options help limit the pace at which replica submits new transactions
  to WAL: the limits are checked every time a transaction from master is
  submitted to replica's WAL, and the space taken by a transaction is
  considered empty once it's successfully written (gh-5536).
