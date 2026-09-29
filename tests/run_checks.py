#!/usr/bin/env python3
"""Compile and run Native Hunts domain, gameplay, and content checks."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import json
import re
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def compiler() -> str:
    configured = os.environ.get("CXX")
    if configured:
        return configured
    for candidate in ("g++", "clang++"):
        resolved = shutil.which(candidate)
        if resolved:
            return resolved
    raise RuntimeError("No C++17 compiler found; set CXX to a compiler path")


def run_domain_tests() -> None:
    # Cygwin toolchains cannot reliably execute binaries from the Windows user
    # temp directory in restricted environments. Keep ephemeral output beneath
    # the repository, matching AzerothCore module test conventions.
    with tempfile.TemporaryDirectory(prefix=".native-hunts-m1-", dir=ROOT) as directory:
        executable = Path(directory) / ("hunt_domain_tests.exe" if os.name == "nt" else "hunt_domain_tests")
        command = [compiler(), "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic", "-Isrc",
            "tests/hunt_domain_tests.cpp", "src/HuntDomain.cpp", "src/HuntGameplay.cpp",
            "src/HuntSnapshot.cpp", "src/HuntCatalog.cpp", "src/HuntRewards.cpp", "-o", str(executable)]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(executable)], cwd=ROOT, check=True)
        print("PASS hunt domain, gameplay, recovery, and snapshot tests", flush=True)


def run_content_checks() -> None:
    epf = ROOT / "content" / "mod-native-hunts.epf"
    with zipfile.ZipFile(epf) as archive:
        if archive.namelist() != ["manifest.json"]:
            raise AssertionError("EPF must contain only manifest.json")
        manifest = json.loads(archive.read("manifest.json"))
    if (manifest.get("package") != "mod-native-hunts" or
            manifest.get("schema") != 2 or manifest.get("version") != "6"):
        raise AssertionError("invalid Native Hunts EPF identity")
    spells = {row["symbol"]: row for row in manifest.get("spells", [])}
    expected_spells = {
        "active-standard-hunt": (1494, "Standard Hunt",
            "You are tracking a Standard Hunt target.",
            "A Standard Hunt is active."),
        "active-elite-hunt": (67823, "Elite Hunt",
            "You are tracking an Elite Hunt target.",
            "An Elite Hunt is active."),
    }
    if set(spells) != set(expected_spells):
        raise AssertionError("managed active-Hunt spell set is incorrect")
    for symbol, (donor, name, description, aura_description) in expected_spells.items():
        row = spells[symbol]
        if (row != {
                "symbol": symbol,
                "copyFrom": donor,
                "profile": "informational-self-aura-v1",
                "name": {"enUS": name},
                "description": {"enUS": description},
                "auraDescription": {"enUS": aura_description},
                "iconCopyFromSpell": donor,
        }):
            raise AssertionError(f"managed active-Hunt spell is incorrect: {symbol}")
    creature_symbols = {row["symbol"] for row in manifest["creatureTemplates"]}
    expected_huntmasters = {
        "huntmaster-corvin", "huntmaster-brannoc", "huntmaster-shalara",
        "huntmaster-veylan", "huntmaster-gorrak", "huntmaster-tahu",
        "huntmaster-morcant", "huntmaster-vaelith", "huntmaster-raleth",
        "huntmaster-varyn",
    }
    expected_prey = {
        "prey-ashfang", "prey-silkmaw", "prey-gorehide", "prey-whiteclaw",
        "prey-tidefang", "prey-stonegut", "prey-sootfang", "prey-nightfang",
        "prey-shadowclaw", "prey-dreadwing", "prey-venomtail", "prey-stormcoil",
        "prey-mirejaw", "prey-razortalon", "prey-cliffhowl", "prey-grimmaw",
    }

    expected_elite = {"elite-oathbreaker", "elite-winterborn", "elite-headsman",
        "elite-veiled-knife", "elite-ashen-pact", "elite-wildclaw",
        "elite-stormcaller", "elite-dusk-confessor", "elite-gravebound",
        "elite-farstrider"}
    if not (expected_huntmasters | expected_prey | expected_elite) <= creature_symbols:
        raise AssertionError("missing managed Huntmaster or standard-prey template")
    spawn_symbols = {row["symbol"] for row in manifest["creatureSpawns"]}
    if spawn_symbols != {f"{symbol}-spawn" for symbol in expected_huntmasters}:
        raise AssertionError("managed Huntmaster spawn set is incomplete")
    objects = {row["symbol"] for row in manifest["gameobjectTemplates"]}
    if objects != {"prey-trail-crystal", "return-rift"}:
        raise AssertionError("runtime gameobject template set is incorrect")
    scripted_objects = {row["symbol"]: row for row in manifest["gameobjectTemplates"]}
    for symbol, display_id in (("prey-trail-crystal", 7942), ("return-rift", 1327)):
        row = scripted_objects[symbol]
        overrides = row["overrides"]
        expected_script = ("mod_native_hunts_trail_crystal" if
                           symbol == "prey-trail-crystal" else
                           "mod_native_hunts_return_rift")
        if (row["copyFrom"] != 19529 or overrides.get("type") != 1 or
                overrides.get("displayId") != display_id or
                overrides.get("scriptName") != expected_script):
            raise AssertionError(f"{symbol} must use the clean scripted-button contract")
    item_symbols = {row["symbol"] for row in manifest["dbcRows"]}
    if "huntmaster-seal" not in item_symbols:
        raise AssertionError("physical Huntmaster's Seal is not declared")
    serialized = json.dumps(manifest)
    for disposable in ("test-creature", "test-creature-spawn", "test-object"):
        if disposable in serialized:
            raise AssertionError(f"disposable resource remains: {disposable}")
    print("PASS managed Native Hunts EPF contract", flush=True)


def run_source_safety_checks() -> None:
    prohibited = {
        "legacy package key": '"mod-hunts"',
        "old loader": "Addmod_huntsScripts",
        "old runtime table": "`hunt_runtime`",
        "old statistics table": "`hunt_stats`",
        "old import table": "`hunt_currency_realm`",
        "old migration component": "HuntCurrencyMigration",
        "old client protocol": '"HUNTS"',
        "hard-coded old crystal entry": "14999010",
        "hard-coded old rift entry": "14999011",
        "hard-coded old Huntmaster entry": "14999980",
        "temporary Native Hunts debug log": "[Native Hunts DEBUG]",
    }
    production = list((ROOT / "src").glob("**/*")) + list((ROOT / "conf").glob("**/*"))
    for path in production:
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8")
        for description, token in prohibited.items():
            if token in text:
                raise AssertionError(f"{description} found in {path.relative_to(ROOT)}: {token}")
    obsolete_loader = ROOT / "src" / "mod_hunts_loader.cpp"
    if obsolete_loader.exists():
        raise AssertionError(f"obsolete milestone content remains: {obsolete_loader.relative_to(ROOT)}")
    required_schema = {
        ROOT / "data/sql/db-characters/base/001_native_hunt_assignment.sql",
        ROOT / "data/sql/db-characters/base/002_native_hunt_stats.sql",
    }
    if not all(path.is_file() for path in required_schema):
        raise AssertionError("native persistence schema is incomplete")
    world_sql = list((ROOT / "data" / "sql" / "db-world").glob("**/*"))
    for path in world_sql:
        if path.is_file() and "native_hunt_assignment" in path.read_text(encoding="utf-8"):
            raise AssertionError("character assignment schema leaked into world SQL")
    assignment = (ROOT / "data/sql/db-characters/base/001_native_hunt_assignment.sql").read_text(encoding="utf-8")
    for column in ("`ambushes_completed`", "`ambush_pending`"):
        if column not in assignment:
            raise AssertionError(f"persistent ambush column missing: {column}")
    updates = "\n".join(path.read_text(encoding="utf-8") for path in
        (ROOT / "data/sql/db-characters/updates").glob("*.sql"))
    if "ADD COLUMN IF NOT EXISTS" in updates:
        raise AssertionError("unsupported updater column syntax present")
    backfill_path = (ROOT / "data/sql/db-characters/updates/"
        "2026_09_27_01_native_hunt_standard_backfill.sql")
    if not backfill_path.is_file():
        raise AssertionError("convergent Standard-completion backfill is missing")
    backfill = backfill_path.read_text(encoding="utf-8")
    for token in ("UPDATE `native_hunt_stats`", "GREATEST(",
                  "`lifetime_completed` - `elite_completed`",
                  "WHERE `standard_completed` <"):
        if token not in backfill:
            raise AssertionError(f"Standard-completion backfill safeguard missing: {token}")
    def migrated_standard(lifetime: int, elite: int, standard: int) -> int:
        return max(standard, lifetime - elite if lifetime >= elite else 0)
    migration_cases = (
        (10, 0, 0, 10),
        (11, 0, 1, 11),
        (12, 2, 10, 10),
        (12, 2, 11, 11),
        (5, 7, 3, 3),
    )
    for lifetime, elite, standard, expected in migration_cases:
        if migrated_standard(lifetime, elite, standard) != expected:
            raise AssertionError("Standard-completion migration semantics regressed")
    manager = (ROOT / "src" / "NativeHuntsManager.cpp").read_text(encoding="utf-8")
    required_turn_in_safety = (
        "SaveInventoryAndGoldToDB(transaction)",
        "CommitCharacterTransactionAndWait(transaction)",
        "DestroyItemCount(_resources.SealItemEntry",
        "duplicate rewards are blocked until recovery",
        "provisional rewards were removed",
    )
    for token in required_turn_in_safety:
        if token not in manager:
            raise AssertionError(f"turn-in consistency safeguard missing: {token}")
    module = (ROOT / "src" / "NativeHuntsModule.cpp").read_text(encoding="utf-8")
    menu_start = module.index("static void ShowMenu(Player *player, Creature *creature)")
    menu_end = module.index("\n\t}\n};", menu_start)
    menu = module[menu_start:menu_end]
    idle = menu.index("if (!runtime) {")
    standard = menu.index("ActionRequest);", idle)
    elite = menu.index("ActionRequestElite);", standard)
    ready = menu.index("} else if (runtime->Aggregate.State", elite)
    record = menu.index("ActionStats);", ready)
    if not idle < standard < elite < ready < record or "if (!runtime &&" in menu:
        raise AssertionError("Huntmaster idle/Elite gossip can dereference a null runtime")
    config = (ROOT / "conf" / "mod_native_hunts.conf.dist").read_text(encoding="utf-8")
    config_reads = set(re.findall(r'"(NativeHunts\.[A-Za-z0-9.]+)"', module))
    config_entries = set(re.findall(r'^(NativeHunts\.[A-Za-z0-9.]+)\s*=', config, re.MULTILINE))
    if config_reads - config_entries:
        raise AssertionError(f"undocumented config reads: {sorted(config_reads - config_entries)}")
    expected_defaults = (
        "NativeHunts.AssignmentScope = Local",
        "NativeHunts.GroupCreditRadius = 100.0",
        "NativeHunts.Ambush.Count = 2",
        "NativeHunts.XPMultiplier = 0.75",
        "NativeHunts.Ambush.EscapeHealthPercent = 50.0",
        "NativeHunts.ReturnRift.ArrivalDistance = 3.0",
    )
    for line in expected_defaults:
        if line not in config:
            raise AssertionError(f"configuration default missing: {line}")
    for token in ("SMSG_GOSSIP_POI", '"nativehunts"', '"status"', '"reset"'):
        if token not in module + manager:
            raise AssertionError(f"PTR feedback/diagnostic surface missing: {token}")
    for token in ("Tracking complete.", "Prey Trail Crystal", "AmbushGuid.Clear()",
                  "AmbushPending = false", "final_site="):
        if token not in manager:
            raise AssertionError(f"feedback/ambush/status behavior missing: {token}")
    for token in ('GameObjectScript("mod_native_hunts_trail_crystal")',
                  'GameObjectScript("mod_native_hunts_return_rift")',
                  "GAMEOBJECT_TYPE_BUTTON", "value->button.lockId == 0",
                  "value->button.autoCloseTime == 0",
                  "value->button.linkedTrap == 0", "SetGoState(GO_STATE_READY)",
                  "GO_FLAG_NOT_SELECTABLE"):
        if token not in module + manager:
            raise AssertionError(f"scripted GameObject interaction contract missing: {token}")
    for token in ("Where is the Huntmaster?", "PrepareGossipMenu", "SMSG_GOSSIP_POI",
                  "ResolveGuardLocators", "gossip_menu_id"):
        if token not in module + manager:
            raise AssertionError(f"guard locator behavior missing: {token}")
    for token in ("StandardRewardQuality", "EliteSealReward", "EliteRewardRequireUpgrade",
                  "SaveInventoryAndGoldToDB(transaction)"):
        if token not in manager:
            raise AssertionError(f"reward behavior missing: {token}")
    aura_contract = (
        'resolveAura("active-standard-hunt"',
        'resolveAura("active-elite-hunt"',
        'Package, symbol, "spell.id"',
        "ReconcileHuntAura(player, nullptr)",
        "player->AddAura(decision.AddSpell, player)",
        "player->RemoveAurasDueToSpell(_resources.StandardHuntAuraSpell)",
        "player->RemoveAurasDueToSpell(_resources.EliteHuntAuraSpell)",
    )
    for token in aura_contract:
        if token not in manager:
            raise AssertionError(f"managed active-Hunt aura contract missing: {token}")
    if manager.count("player->AddAura(") != 1 or "OnPlayerLogin" not in module:
        raise AssertionError("active-Hunt aura reconciliation is not centralized")
    print("PASS native-only source and persistence safety checks", flush=True)


def main() -> None:
    run_domain_tests()
    run_content_checks()
    run_source_safety_checks()


if __name__ == "__main__":
    main()
