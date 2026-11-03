# Test racers

`make-test-roster.php` adds made-up racers to your **existing** racing groups,
with stand-in photos, so you can try check-in, the Print Licenses page,
scheduling and awards before real racers exist.

## Do not use "Build Fake Roster" on a database you have set up

DerbyNet's own fake roster files its racers in new "Fake Lions", "Fake Tigers",
... groups and sets a `fake-racers` flag.  When you later choose **Purge racers**
on the Set-Up page, that flag makes DerbyNet delete *every* group, subgroup and
aggregate group (and the pack-standings setting), including the ones you built
for the real event.  Use it only on an empty database.

## Using this script

1. **Take a backup first, and check it is not empty** (a backup taken while the
   server is busy can silently fail):

   ```
   B=/var/lib/derbynet/<year>/<database>/derbynet.sqlite3.TEMPLATE
   sudo -u www-data sqlite3 /var/lib/derbynet/<year>/<database>/derbynet.sqlite3 ".backup $B"
   ls -la $B        # must be megabytes, not 0 bytes
   ```
2. `cd /var/www/html/derbynet && sudo -u www-data php /path/to/make-test-roster.php 8 6`
   (8 racers per group, 6 in Outlaw).  It refuses to run if racers already exist.
3. Test racers have the note `TESTROSTER`, car numbers from the normal numbering
   rules, photos named `testroster_*`, and a few are deliberately missing a photo
   or not checked in.

## Getting back to a clean state

Restore the backup into the live database (SQLite takes care of locking), then
remove the leftover photos and the card-print log:

```
sudo -u www-data sqlite3 /var/lib/derbynet/<year>/<database>/derbynet.sqlite3 ".restore $B"
sudo rm -f /var/lib/derbynet/<year>/<database>/racers/testroster_* \
           /var/lib/derbynet/<year>/<database>/cars/testroster_*
```

The restore also removes any `PrintJobs` log rows (cards sent), since that table
lives in the same database.  Restoring replaces everything changed since the
backup, so do it before the real roster goes in.
