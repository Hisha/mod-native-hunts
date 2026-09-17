# Native-only validation

Run from the module root:

```sh
python3 -B tests/run_native_checks.py
# Optional read-only compile check against a configured reference core:
python3 -B tests/run_native_checks.py --compile-commands /path/to/build/compile_commands.json
```

The optional reference command uses the existing mod-dungeon-quests compilation
entry for core include paths/definitions; it requires that entry and valid local
headers. It uses `-fsyntax-only`, does not reconfigure or write the reference
core/build, and is not a linked worldserver build. All temporary executables
stay inside this repository and are removed after the run.

## Database-free coverage

- `native_currency_startup.cpp`: production service + migration with memory
  adapters. Missing/inactive/invalid/duplicate CM provider, physical balance and
  non-deployment allocated IDs, wrong cost/extra requirement, priced merchandise,
  missing/non-stackable Seal template, recorded version/identity/state mismatch,
  startup-only guard, existing NATIVE receipt, and blocked rewards after losing
  the provider on reinitialization. No real databases or player sessions.
- `transaction_wait_tests.cpp`: actual helper and migration implementation,
  worker future true/false/invalid/exception paths, failed snapshot/delivery/final
  transactions, receipt failures and publish-after-verification ordering.
- `test_extended_cost_epf.py`: unchanged 4.3.0 symbolic declarations and five-Seal
  vendor proof. Optional `--previous-epf` accepts a saved 4.2.0 fixture. The runner
  also compares the authored JSON with the EPF manifest.

## Optional isolated SQL integration harnesses

`currency_service_mysql.cpp` now expects **unavailable** for a missing/inactive
provider, even before migration. It retains migration, physical reward/balance,
mail rollback/retry and durable-receipt checks. `realm_migration_mysql.cpp`
retains the existing import scenarios. These require a disposable MySQL fixture
named `phase4_test`, never a realm database. Their fixture/setup instructions
are below; current source expectations supersede old runtime-mode assertions. Compile with
`tests/currency_adapter`, `src`, and the MySQL client headers/library. Do not
use `NATIVE_HUNTS_MEMORY_DATABASE` for these SQL tests.

The cleanup pass compiled these SQL harnesses but did not execute SQL tests,
start a database/worldserver, or claim fresh live-client acceptance. Older
captured results and CM-specific harnesses are historical evidence, not results
for this revision. `extended_cost_consumer_mysql.cpp` is an earlier Phase 5 CM
fixture and still requires its original package version and external CM code.

## Required realm acceptance after rebuild

1. Confirm the new loader and `NativeHunts.*` settings load, with no old server
   module simultaneously installed. Verify prior custom settings were migrated.
2. With matching ACTIVE CM content, verify ready startup, retained allocations,
   unchanged existing migration receipts and physical balances across restart.
3. On isolated fresh/legacy-import fixtures, check zero-balance startup and
   offline mail delivery without duplicate issuance after restart/failure.
4. Test normal/Elite hunts, ambush/prey, tracking, rewards, Hunting Record,
   Return Rift, class/spec filters and existing authoring commands.
5. Verify the one-item MerchantFrame purchase: five physical Seals, insufficient
   funds, ineligible class/level, forged slot/count/item and wrong Huntmaster.
6. On an isolated fixture with absent/invalid CM content, confirm clear startup
   errors, blocked turn-in/vendor/reward operations, and no virtual spending.

No client attestation or new UI is added. Operators remain responsible for
publishing/installing the matching native client content.

## Disposable SQL fixture reproduction (not executed in cleanup)

Create the local `build` output directory first. The commands below are optional
fixture instructions, not steps for an existing realm.

For the two Hunt SQL tests, compile the named production .cpp files with `tests/currency_adapter` before `src` in the include path and link the MySQL client library, for example:

```sh
g++ -std=c++17 $(mysql_config --cflags) \
  -Imodules/mod-native-hunts/tests/currency_adapter -Imodules/mod-native-hunts/src \
  modules/mod-native-hunts/tests/currency_service_mysql.cpp \
  modules/mod-native-hunts/src/HuntCurrencyService.cpp \
  modules/mod-native-hunts/src/HuntCurrencyMigration.cpp \
  $(mysql_config --libs) -o ./build/hunt-currency-service-test
```

Create a fresh disposable `phase4_test`, import `004_hunt_currency.sql`, then create these test-only tables:

```sql
CREATE TABLE hunt_stats(guid INT UNSIGNED PRIMARY KEY,huntmaster_seals INT UNSIGNED NOT NULL) ENGINE=InnoDB;
CREATE TABLE characters(guid INT UNSIGNED PRIMARY KEY) ENGINE=InnoDB;
CREATE TABLE hunt_runtime(guid INT UNSIGNED PRIMARY KEY) ENGINE=InnoDB;
CREATE TABLE item_instance(guid INT PRIMARY KEY,owner_guid INT,itemEntry INT,count INT) ENGINE=InnoDB;
CREATE TABLE mail_items(mail_id INT,item_guid INT PRIMARY KEY,receiver INT) ENGINE=InnoDB;
CREATE TABLE mail(id INT PRIMARY KEY,messageType INT,stationery INT,mailTemplateId INT,sender INT,receiver INT,
 subject TEXT,body TEXT,has_items INT,expire_time BIGINT UNSIGNED,deliver_time BIGINT UNSIGNED,money INT,cod INT,checked INT) ENGINE=InnoDB;
```

Run `./build/hunt-currency-service-test /absolute/private/mysql.sock`. For the migration test, use a separately reset fixture, compile `realm_migration_mysql.cpp` plus `HuntCurrencyMigration.cpp`, and add `delivery_items(id INT AUTO_INCREMENT PRIMARY KEY,guid INT,itemEntry INT,amount INT) ENGINE=InnoDB`. It needs the realm/delivery schema plus the simple hunt_stats/characters tables above.
