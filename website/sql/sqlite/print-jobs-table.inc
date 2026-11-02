<?php
// Ledger of license cards sent to the card printer.  Created on first use.
return array(

"CREATE TABLE IF NOT EXISTS PrintJobs ("
."  jobid     INTEGER PRIMARY KEY AUTOINCREMENT,"
."  racerid   INTEGER NOT NULL,"
."  submitted INTEGER NOT NULL,"  // Unix time
."  cups_id   VARCHAR(80),"       // e.g., ZC300-14
."  sides     INTEGER,"
."  status    VARCHAR(20),"       // 'submitted' or 'failed'
."  message   VARCHAR(300)"
."  )",
);
?>
