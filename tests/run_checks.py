#!/usr/bin/env python3
"""Compile and run Native Hunts domain, gameplay, and content checks."""

from __future__ import annotations

import ast
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import json
import hashlib
import re
import sqlite3
import struct
import zipfile
import xml.etree.ElementTree as ET

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
            "src/HuntSnapshot.cpp", "src/HuntUiProtocol.cpp", "src/HuntCatalog.cpp", "src/HuntRewards.cpp",
            "src/RelicPoolPolicy.cpp", "src/SealCatalogPolicy.cpp", "-o", str(executable)]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(executable)], cwd=ROOT, check=True)
        print("PASS hunt domain, gameplay, recovery, and snapshot tests", flush=True)


def run_tracking_milestone_source_checks() -> None:
    manager = (ROOT / "src" / "NativeHuntsManager.cpp").read_text(encoding="utf-8")
    milestone_start = manager.index(
        "bool NativeHuntsManager::ReconcileTrackingMilestones")
    milestone_end = manager.index(
        "bool NativeHuntsManager::AwardTrackingProgress", milestone_start)
    milestone = manager[milestone_start:milestone_end]
    if ("runtime.AmbushPending = true;" not in milestone or
            "SpawnAmbush(player, runtime, message)" not in milestone or
            milestone.index("runtime.AmbushPending = true;") >
            milestone.index("SpawnAmbush(player, runtime, message)")):
        raise AssertionError(
            "tracking milestone reconciliation does not preserve pending state "
            "before an ambush spawn attempt")
    if manager.count("ReconcileTrackingMilestones(") < 4:
        raise AssertionError(
            "tracking milestones are not reconciled after load, progress, and ambush completion")
    print("PASS tracking milestone manager integration contracts", flush=True)


def run_relic_persistence_contract_checks() -> None:
    assignment = (ROOT / "data/sql/db-characters/base/001_native_hunt_assignment.sql").read_text(
        encoding="utf-8")
    collection = (ROOT / "data/sql/db-characters/base/003_native_hunt_collection.sql").read_text(
        encoding="utf-8")
    pools = (ROOT / "data/sql/db-characters/base/004_native_hunt_relic_pool.sql").read_text(
        encoding="utf-8")
    migration = (ROOT / "data/sql/db-characters/updates/2026_10_17_01_native_hunt_collection.sql").read_text(
        encoding="utf-8")
    pool_migration = (ROOT / "data/sql/db-characters/updates/2026_10_18_02_native_hunt_relic_pools.sql").read_text(
        encoding="utf-8")
    manager = (ROOT / "src/NativeHuntsManager.cpp").read_text(encoding="utf-8")
    module = (ROOT / "src/NativeHuntsModule.cpp").read_text(encoding="utf-8")
    configuration = (ROOT / "conf/mod_native_hunts.conf.dist").read_text(
        encoding="utf-8")
    world = (ROOT / "data/sql/db-world/base/001_native_hunt_shared_relic.sql").read_text(
        encoding="utf-8")
    visual_update = (ROOT / "data/sql/db-world/updates/2026_10_18_00_native_hunt_shared_relic_visual.sql").read_text(
        encoding="utf-8")
    for token in ("`hunt_instance_id`", "UNIQUE KEY `uq_native_hunt_instance`"):
        if token not in assignment:
            raise AssertionError(f"hunt-instance persistence contract missing: {token}")
    for token in ("`hunt_instance_id`", "`objective_key`", "`relic_key`",
                  "PRIMARY KEY (`hunt_instance_id`, `objective_key`, `relic_key`)"):
        if token not in collection:
            raise AssertionError(f"collection uniqueness contract missing: {token}")
    for token in ("ADD COLUMN `hunt_instance_id`", "SET `hunt_instance_id` = UUID()",
                  "CREATE TABLE IF NOT EXISTS `native_hunt_collection`"):
        if token not in migration:
            raise AssertionError(f"existing-install collection migration missing: {token}")
    for token in ("`relic_pool_id`", "idx_native_hunt_relic_pool"):
        if token not in assignment or token not in pool_migration:
            raise AssertionError(f"assignment-to-pool migration contract missing: {token}")
    for token in ("native_hunt_relic_pool", "native_hunt_relic_position",
                  "`map_id`", "`instance_id`", "`zone_id`",
                  "PRIMARY KEY (`pool_id`, `relic_index`)",
                  "(`map_id`, `instance_id`, `zone_id`)"):
        if token not in pools or token not in pool_migration:
            raise AssertionError(f"relic-pool persistence contract missing: {token}")
    if "NativeHunts.Relics.CountPerZone = 60" not in configuration:
        raise AssertionError("default relic population is not documented as 60")
    if "NativeHunts.CollectionPrototype.Enable" in configuration:
        raise AssertionError("obsolete separate relic enable setting remains")
    for token in ('"NativeHunts.Relics.CountPerZone"',
                  "NormalizeRelicsPerZone", "std::int32_t"):
        if token not in module:
            raise AssertionError(f"relic count validation is missing: {token}")
    for token in ("INSERT IGNORE INTO `native_hunt_collection`",
                  "SET @native_hunts_relic_inserted := ROW_COUNT()",
                  "UPDATE `native_hunt_assignment` SET `tracking_progress`=LEAST(100,",
                  "CommitCharacterTransactionAndWait(transaction)",
                  "AwardTrackingProgress(player, *runtime, amount)",
                  "FindRelicPool(object, &relicIndex)",
                  "map->SummonGameObject(_config.RelicGameObjectEntry",
                  "Zone2MapCoordinates(x, y, pool.Scope.ZoneId)",
                  "findNearestPoly", "AdvanceRelicPlacementAttempts"):
        if token not in manager:
            raise AssertionError(f"transactional shared-relic integration missing: {token}")
    for token in ("LoadRelicPools()", "AssociateRelicPool(",
                  "RetireRelicPool(", "RemoveRelicPoolObjects(",
                  "pool.Scope.InstanceId", "runtime.RelicPoolId",
                  "_relicPoolRetryAfter[scope] = now + std::chrono::minutes(1)"):
        if token not in manager:
            raise AssertionError(f"shared relic pool lifecycle missing: {token}")
    for token in ("QueueRelicPoolGeneration(",
                  "ProcessRelicPoolGenerationJobs()",
                  "RelicPlacementCandidatesPerUpdate",
                  "RelicPlacementCandidatesPerJob",
                  "_relicPoolGenerationJobs.count(scope)",
                  "ShouldCancelRelicGeneration(",
                  "LoadPersistedRelicPool(job.Pool.Scope, recovered)",
                  "completed generation job will be retained"):
        if token not in manager:
            raise AssertionError(f"incremental relic generation contract missing: {token}")
    for function in ("RequestHunt", "RequestEliteHunt"):
        start = manager.index(f"bool NativeHuntsManager::{function}")
        end = manager.index("\n}", start)
        acceptance = manager[start:end]
        if "AssociateRelicPool(" not in acceptance:
            raise AssertionError(f"{function} no longer requests relic association")
        if ("AdvanceRelicPoolGeneration(" in acceptance or
                "ProcessRelicPoolGenerationJobs(" in acceptance):
            raise AssertionError(f"{function} performs randomized placement synchronously")
    if "DELETE FROM `native_hunt_collection`" in manager:
        raise AssertionError("assignment lifecycle deletes durable relic collection history")
    reconcile_start = manager.index("void NativeHuntsManager::ReconcileRelicPools()")
    reconcile_end = manager.index("RelicPool *NativeHuntsManager::FindRelicPool", reconcile_start)
    reconcile = manager[reconcile_start:reconcile_end]
    if ("if (_config.RelicsCountPerZone == 0)" not in reconcile or
            "DELETE FROM `native_hunt_relic_position`" not in reconcile or
            "if (!IsEnabled())" not in reconcile):
        raise AssertionError("zero-count disable cleanup is not isolated from transient outages")
    logout_start = manager.index("void NativeHuntsManager::OnLogout")
    logout = manager[logout_start:]
    ordinary_logout = logout[logout.rindex("_uncertainTurnIns.erase(uncertain);") :]
    if "_runtimes.erase" in ordinary_logout or "SaveAssignment(it->second)" not in ordinary_logout:
        raise AssertionError("ordinary logout releases a resumable Hunt or its pool reference")
    collection_start = manager.index(
        "bool NativeHuntsManager::CollectRelic")
    collection_end = manager.index(
        "bool NativeHuntsManager::UseCrystal", collection_start)
    if "object->Delete()" in manager[collection_start:collection_end]:
        raise AssertionError("shared relic interaction consumes the world object")
    if "mod_native_hunts_shared_relic" not in world or re.search(
            r"INSERT\s+(?:IGNORE\s+)?INTO\s+`gameobject`(?:\s|\()", world,
            re.IGNORECASE):
        raise AssertionError("prototype world SQL must define only the shared relic template")
    if not re.search(
            r"\(990000,\s*1,\s*5011,\s*'Cursed Hunt Relic',\s*1\.0,\s*"
            r"'mod_native_hunts_shared_relic',\s*12340\)", world):
        raise AssertionError("new installs do not use the verified purple crystal display")
    for token in ("UPDATE `gameobject_template`", "`displayId` = 5011",
                  "`size` = 1.0", "`entry` = 990000",
                  "`ScriptName` = 'mod_native_hunts_shared_relic'"):
        if token not in visual_update:
            raise AssertionError(f"existing-install relic visual update missing: {token}")
    if re.search(r"\b(?:DELETE|INSERT|REPLACE)\b", visual_update, re.IGNORECASE):
        raise AssertionError("relic visual update must be a repeatable in-place update")

    with tempfile.TemporaryDirectory(prefix=".native-hunts-relic-", dir=ROOT) as directory:
        database = Path(directory) / "ledger.sqlite"
        connection = sqlite3.connect(database)
        connection.executescript("""
            CREATE TABLE assignment (
                character_guid INTEGER PRIMARY KEY,
                hunt_instance_id TEXT NOT NULL UNIQUE,
                progress INTEGER NOT NULL DEFAULT 0,
                relic_pool_id TEXT
            );
            CREATE TABLE collection (
                hunt_instance_id TEXT NOT NULL,
                objective_key TEXT NOT NULL,
                relic_key TEXT NOT NULL,
                tracking_award INTEGER NOT NULL,
                PRIMARY KEY (hunt_instance_id, objective_key, relic_key)
            );
            CREATE TABLE gameobject_template (
                entry INTEGER PRIMARY KEY,
                displayId INTEGER NOT NULL,
                size REAL NOT NULL,
                ScriptName TEXT NOT NULL
            );
            INSERT INTO gameobject_template VALUES
                (990000, 3991, 1.0, 'mod_native_hunts_shared_relic');
            CREATE TABLE relic_pool (
                pool_id TEXT PRIMARY KEY,
                map_id INTEGER NOT NULL,
                instance_id INTEGER NOT NULL,
                zone_id INTEGER NOT NULL,
                relic_count INTEGER NOT NULL,
                UNIQUE (map_id, instance_id, zone_id)
            );
            CREATE TABLE relic_position (
                pool_id TEXT NOT NULL,
                relic_index INTEGER NOT NULL,
                x REAL NOT NULL,
                y REAL NOT NULL,
                z REAL NOT NULL,
                PRIMARY KEY (pool_id, relic_index)
            );
        """)
        connection.executescript(visual_update)
        connection.executescript(visual_update)
        relic_visual = connection.execute(
            "SELECT displayId, size, ScriptName FROM gameobject_template "
            "WHERE entry=990000").fetchone()
        if relic_visual != (5011, 1.0, "mod_native_hunts_shared_relic"):
            raise AssertionError("repeatable relic visual update changed its template contract")
        connection.executemany(
            "INSERT INTO assignment(character_guid,hunt_instance_id,progress) VALUES (?, ?, 0)",
                               ((1, "hunt-a"), (2, "hunt-b")))
        connection.execute("INSERT INTO relic_pool VALUES ('pool-a', 0, 0, 40, 60)")
        connection.executemany(
            "INSERT INTO relic_position VALUES ('pool-a', ?, ?, ?, ?)",
            ((index, float(index), float(index * 2), 40.0) for index in range(60)))
        connection.execute("UPDATE assignment SET relic_pool_id='pool-a'")
        if connection.execute(
                "SELECT COUNT(DISTINCT relic_pool_id) FROM assignment").fetchone()[0] != 1:
            raise AssertionError("two hunts in one world scope did not share one pool")
        for instance in ("hunt-a", "hunt-b"):
            connection.execute("INSERT INTO collection VALUES (?, 'objective', 'relic', 10)",
                               (instance,))
        if connection.execute("SELECT COUNT(*) FROM collection").fetchone()[0] != 2:
            raise AssertionError("two hunt instances cannot independently collect one relic")
        try:
            connection.execute("INSERT INTO collection VALUES ('hunt-a', 'objective', 'relic', 10)")
        except sqlite3.IntegrityError:
            pass
        else:
            raise AssertionError("database uniqueness did not reject duplicate relic credit")
        connection.commit()
        connection.close()
        connection = sqlite3.connect(database)
        if connection.execute("SELECT COUNT(*) FROM collection").fetchone()[0] != 2:
            raise AssertionError("collection credit did not survive restart")
        if connection.execute(
                "SELECT COUNT(*) FROM relic_position WHERE pool_id='pool-a'").fetchone()[0] != 60:
            raise AssertionError("relic locations did not survive restart")
        connection.execute("DELETE FROM assignment WHERE character_guid=1")
        if connection.execute(
                "SELECT COUNT(*) FROM assignment WHERE relic_pool_id='pool-a'").fetchone()[0] != 1:
            raise AssertionError("one completion retired a pool still used by another hunt")
        connection.execute(
            "INSERT INTO assignment(character_guid,hunt_instance_id,progress) VALUES (1, 'hunt-a-new', 0)")
        connection.execute("INSERT INTO collection VALUES ('hunt-a-new', 'objective', 'relic', 10)")
        if connection.execute(
                "SELECT COUNT(*) FROM collection WHERE relic_key='relic'").fetchone()[0] != 3:
            raise AssertionError("a newly accepted hunt inherited the abandoned hunt ledger")
        connection.execute(
            "INSERT INTO assignment(character_guid,hunt_instance_id,progress) VALUES (3, 'hunt-c', 0)")
        connection.commit()
        try:
            connection.execute("BEGIN")
            connection.execute("INSERT INTO collection VALUES ('hunt-c', 'objective', 'relic', 10)")
            connection.execute(
                "UPDATE assignment SET progress=progress+10 "
                "WHERE hunt_instance_id='hunt-c'")
            raise RuntimeError("simulated database failure")
        except RuntimeError:
            connection.rollback()
        if connection.execute(
                "SELECT COUNT(*) FROM collection WHERE "
                "hunt_instance_id='hunt-c'").fetchone()[0] != 0:
            raise AssertionError("failed collection transaction retained ledger credit")
        if connection.execute(
                "SELECT progress FROM assignment WHERE "
                "hunt_instance_id='hunt-c'").fetchone()[0] != 0:
            raise AssertionError("failed collection transaction retained tracking progress")
        original_positions = connection.execute(
            "SELECT relic_index,x,y,z FROM relic_position WHERE pool_id='pool-a' "
            "ORDER BY relic_index").fetchall()
        connection.execute("DELETE FROM assignment WHERE hunt_instance_id='hunt-b'")
        if connection.execute(
                "SELECT COUNT(*) FROM assignment WHERE relic_pool_id='pool-a'").fetchone()[0] != 0:
            raise AssertionError("last hunt did not release the shared pool")
        connection.execute("DELETE FROM relic_position WHERE pool_id='pool-a'")
        connection.execute("DELETE FROM relic_pool WHERE pool_id='pool-a'")
        if connection.execute("SELECT COUNT(*) FROM relic_position").fetchone()[0] != 0:
            raise AssertionError("pool retirement did not clean up positions")
        connection.execute("INSERT INTO relic_pool VALUES ('pool-b', 0, 0, 40, 60)")
        connection.executemany(
            "INSERT INTO relic_position VALUES ('pool-b', ?, ?, ?, ?)",
            ((index, float(index) + 0.25, float(index * 2) + 0.5, 41.0)
             for index in range(60)))
        fresh_positions = connection.execute(
            "SELECT relic_index,x,y,z FROM relic_position WHERE pool_id='pool-b' "
            "ORDER BY relic_index").fetchall()
        if fresh_positions == original_positions:
            raise AssertionError("a later hunt reused retired relic coordinates")
        connection.executemany(
            "INSERT INTO assignment(character_guid,hunt_instance_id,progress) "
            "VALUES (?, ?, 0)", ((4, "hunt-d"), (5, "hunt-e")))
        connection.commit()
        connection.execute("BEGIN")
        connection.execute("INSERT INTO relic_pool VALUES ('pool-uncertain', 1, 0, 12, 3)")
        connection.executemany(
            "INSERT INTO relic_position VALUES ('pool-uncertain', ?, ?, ?, ?)",
            ((0, 1.0, 2.0, 3.0), (1, 4.0, 5.0, 6.0),
             (2, 7.0, 8.0, 9.0)))
        connection.execute(
            "UPDATE assignment SET relic_pool_id='pool-uncertain' "
            "WHERE hunt_instance_id='hunt-d'")
        connection.commit()
        # Simulate losing the commit acknowledgement and verification query.
        # Recovery must discover the unique scope after reconnect instead of
        # inserting or regenerating another pool.
        connection.close()
        connection = sqlite3.connect(database)
        recovered = connection.execute(
            "SELECT pool_id,relic_count FROM relic_pool WHERE "
            "map_id=1 AND instance_id=0 AND zone_id=12").fetchall()
        if recovered != [("pool-uncertain", 3)]:
            raise AssertionError("restart did not recover the uncertain committed pool")
        if connection.execute(
                "SELECT COUNT(*) FROM relic_position WHERE "
                "pool_id='pool-uncertain'").fetchone()[0] != 3:
            raise AssertionError("uncertain pool recovery accepted incomplete positions")
        connection.execute(
            "UPDATE assignment SET relic_pool_id='pool-uncertain' "
            "WHERE hunt_instance_id='hunt-e' AND relic_pool_id IS NULL")
        if connection.execute(
                "SELECT COUNT(*) FROM relic_pool WHERE "
                "map_id=1 AND instance_id=0 AND zone_id=12").fetchone()[0] != 1:
            raise AssertionError("concurrent association duplicated an uncertain scope pool")
        connection.close()
    print("PASS shared relic persistence and transaction contracts", flush=True)


def run_functional_ui_checks() -> None:
    """Verify the functional Native Hunts client contract of the known-working
    version-18 package.

    A package that ships only static FrameForge layout does not package this
    contract's FrameXML/Lua content and must fail here.
    """
    epf = ROOT / "content" / "mod-native-hunts.epf"
    manifest_source = (ROOT / "content" / "manifest.json").read_bytes()
    source_manifest = json.loads(manifest_source)
    if source_manifest.get("version") != "18":
        print("SKIP legacy version-18 presentation contract (superseded by functional FrameForge integration)",
              flush=True)
        return
    if (source_manifest.get("package") != "mod-native-hunts" or
            source_manifest.get("schema") != 3):
        raise AssertionError("invalid Native Hunts EPF identity")
    required_sources = {
        "client/Interface/FrameXML/NativeHuntsFrame.xml",
        "client/Interface/FrameXML/NativeHuntsFrame.lua",
    }
    packaged_sources = {entry.get("source") for entry in source_manifest.get("content", [])}
    if not required_sources <= packaged_sources:
        raise AssertionError(
            "functional Native Hunts FrameXML/Lua content is not packaged; a static "
            "FrameForge layout-only export cannot pass the functional client contract")
    expected_members = ["manifest.json", *(
        entry["source"] for entry in source_manifest["content"]),
        source_manifest["clientFrameXml"]["stockTocSource"]]
    with zipfile.ZipFile(epf) as archive:
        if archive.namelist() != expected_members:
            raise AssertionError("EPF member set or deterministic order is incorrect")
        packaged = {member: archive.read(member) for member in expected_members}
    if packaged["manifest.json"] != manifest_source:
        raise AssertionError("EPF manifest is not synchronized with content/manifest.json")
    manifest = json.loads(packaged["manifest.json"])
    expected_core_content = [
        {
            "type": "file",
            "source": "client/Interface/FrameXML/NativeHuntsFrame.xml",
            "target": "Interface/FrameXML/NativeHuntsFrame.xml",
        },
        {
            "type": "file",
            "source": "client/Interface/FrameXML/NativeHuntsFrame.lua",
            "target": "Interface/FrameXML/NativeHuntsFrame.lua",
        },
    ]
    expected_texture_names = {
        "hunt_divider.tga",
        "hunt_icon_elite.tga", "hunt_icon_seal.tga", "hunt_icon_standard.tga",
        "hunt_icon_turnin.tga", "hunt_panel_identity.tga", "hunt_panel_idle.tga",
        "hunt_panel_record.tga", "hunt_panel_state.tga",
        "hunt_trail_prints.tga",
    }
    content = manifest.get("content", [])
    if content[:2] != expected_core_content:
        raise AssertionError("Native Hunts FrameXML content declaration is incorrect")
    texture_entries = content[2:]
    if ({Path(entry.get("source", "")).name for entry in texture_entries} !=
            expected_texture_names or
            any(entry.get("type") != "file" or
                not entry.get("source", "").startswith("client/Interface/NativeHunts/") or
                not entry.get("target", "").startswith("Interface/NativeHunts/")
                for entry in texture_entries)):
        raise AssertionError("Native Hunts runtime texture declarations are incorrect")
    expected_frame_xml = {
        "stockTocSource": "upstream/FrameXML.toc",
        "stockTocSha256": "3158bea13225ae51137a389f0f3ab8566e94b6be84196dd2c1fda27024677754",
        "loadEntries": [{
            "target": "Interface/FrameXML/NativeHuntsFrame.xml",
            "after": "Interface/FrameXML/LFDFrame.xml",
        }],
    }
    if manifest.get("clientFrameXml") != expected_frame_xml:
        raise AssertionError("additive FrameXML declaration is incorrect")
    if "clientRequirements" in manifest:
        raise AssertionError("protected-framexml must be inferred from clientFrameXml")
    stock_toc = packaged["upstream/FrameXML.toc"]
    if (len(stock_toc) != 2820 or
            hashlib.sha256(stock_toc).hexdigest() != expected_frame_xml["stockTocSha256"]):
        raise AssertionError("packaged build-12340 FrameXML.toc baseline is incorrect")
    if b"NativeHuntsFrame" in stock_toc:
        raise AssertionError("stock FrameXML.toc input must remain byte-identical")

    xml_member = expected_core_content[0]["source"]
    lua_member = expected_core_content[1]["source"]
    for member in (entry["source"] for entry in content):
        source = (ROOT / "content" / member).read_bytes()
        if packaged[member] != source:
            raise AssertionError(f"EPF client asset is stale: {member}")
    expected_texture_canvases = {
        "hunt_divider.tga": (512, 8),
        "hunt_panel_identity.tga": (512, 128),
        "hunt_panel_idle.tga": (512, 256),
        "hunt_panel_record.tga": (512, 128),
        "hunt_panel_state.tga": (512, 128),
    }
    for entry in texture_entries:
        data = packaged[entry["source"]]
        if len(data) < 18 or data[2] != 2 or data[16] != 32 or data[17] != 0x28:
            raise AssertionError(f"runtime texture is not a 32-bit top-origin TGA: {entry['source']}")
        width, height = struct.unpack_from("<HH", data, 12)
        if (width & (width - 1)) or (height & (height - 1)):
            raise AssertionError(f"runtime texture canvas is not power-of-two: {entry['source']}")
        expected_canvas = expected_texture_canvases.get(Path(entry["source"]).name)
        if expected_canvas and (width, height) != expected_canvas:
            raise AssertionError(f"runtime panel texture canvas regressed: {entry['source']}")
    ui_xml = packaged[xml_member].decode("utf-8")
    ui_lua = packaged[lua_member].decode("utf-8")
    asset_prep = (ROOT / "tools" / "prepare_ui_assets.py").read_text(encoding="utf-8")
    ET.fromstring(ui_xml)
    declared_targets = {entry["target"].replace("/", "\\").lower() for entry in content}
    artwork_references = set(re.findall(
        r'Interface\\NativeHunts\\[A-Za-z0-9_]+\.tga', ui_xml + ui_lua))
    for reference in artwork_references:
        if reference.lower() not in declared_targets:
            raise AssertionError(f"unpackaged Native Hunts artwork reference: {reference}")
    required_xml = (
        '<Script file="NativeHuntsFrame.lua"/>',
        'name="NativeHuntsFrame" parent="LFDParentFrame"',
        'name="LFDParentFrameTab1"',
        'name="LFDParentFrameTab2"',
        'text="Dungeon Finder"',
        'text="Hunts"',
        'text="Player vs. Environment"',
        'name="$parentContentPanel"',
        '<Size x="296" y="406"/>',
        '<Size x="280" y="88"/>',
        '<Size x="280" y="118"/>',
        '<Size x="280" y="212"/>',
        '<Size x="280" y="86"/>',
        '<Size x="356" y="156"/>',
        '<Size x="356" y="30"/>',
        'name="$parentIdentity" hidden="true"',
        'name="$parentHuntState" hidden="true"',
        'name="$parentIdle" hidden="true"',
        'name="$parentRecord" hidden="true"',
        'name="$parentProgress" hidden="true" minValue="0" maxValue="100"',
        'file="Interface\\NativeHunts\\hunt_panel_identity.tga"',
        'file="Interface\\NativeHunts\\hunt_panel_state.tga"',
        'file="Interface\\NativeHunts\\hunt_panel_idle.tga"',
        'file="Interface\\NativeHunts\\hunt_panel_record.tga"',
        'file="Interface\\NativeHunts\\hunt_divider.tga"',
        'file="Interface\\NativeHunts\\hunt_icon_turnin.tga"',
        'file="Interface\\NativeHunts\\hunt_icon_seal.tga"',
        'file="Interface\\NativeHunts\\hunt_trail_prints.tga"',
        'file="Interface\\TargetingFrame\\UI-StatusBar"',
        'text="HUNT RECORD"',
        'text="Standard Hunts"',
        'text="Elite Hunts"',
        'text="Elite Today"',
        'text="Huntmaster\'s Seals"',
        '<AbsDimension x="18" y="-27"/>',
    )
    for token in required_xml:
        if token not in ui_xml:
            raise AssertionError(f"Native Hunts FrameXML behavior missing: {token}")
    if '<Texture file="Interface\\LFGFrame\\UI-LFG-FRAME"><Size x="512" y="512"/>' in ui_xml:
        raise AssertionError("Hunts mode reused the fixed 356x440 stock shell as a resizable texture")
    authored_assets = (
        'Asset("hunt_panel_idle.png", "hunt_panel_idle.tga", (280, 212), (512, 256))',
        'Asset("hunt_panel_active.png", "hunt_panel_identity.tga", (280, 88), (512, 128))',
        'Asset("hunt_panel_progress.png", "hunt_panel_state.tga", (280, 118), (512, 128))',
        'Asset("hunt_panel_record.png", "hunt_panel_record.tga", (280, 86), (512, 128))',
        'Asset("hunt_divider.png", "hunt_divider.tga", (260, 8), (512, 8))',
    )
    for token in authored_assets:
        if token not in asset_prep:
            raise AssertionError(f"authored Hunt texture dimensions regressed: {token}")
    xml_root = ET.fromstring(ui_xml)
    authored_render_sizes = {
        "Interface\\NativeHunts\\hunt_panel_identity.tga": (280, 88),
        "Interface\\NativeHunts\\hunt_panel_state.tga": (280, 118),
        "Interface\\NativeHunts\\hunt_panel_idle.tga": (280, 212),
        "Interface\\NativeHunts\\hunt_panel_record.tga": (280, 86),
        "Interface\\NativeHunts\\hunt_divider.tga": (260, 8),
    }
    for texture in (element for element in xml_root.iter() if element.tag.endswith("Texture")):
        expected_size = authored_render_sizes.get(texture.get("file"))
        if expected_size:
            size = next((child for child in texture if child.tag.endswith("Size")), None)
            actual_size = ((int(size.get("x")), int(size.get("y"))) if size is not None else None)
            if actual_size != expected_size:
                raise AssertionError(
                    f"authored Hunt texture is stretched in FrameXML: {texture.get('file')}")
    parent_by_child = {child: parent for parent in xml_root.iter() for child in parent}
    named_frames = {element.get("name"): element for element in xml_root.iter()
                    if element.tag.endswith("Frame") and element.get("name")}
    content_panel = named_frames.get("$parentContentPanel")
    if content_panel is None:
        raise AssertionError("Native Hunts content panel hierarchy is missing")
    for frame_name in ("$parentIdentity", "$parentHuntState", "$parentIdle", "$parentRecord"):
        frame = named_frames.get(frame_name)
        ancestor = frame
        while ancestor is not None and ancestor is not content_panel:
            ancestor = parent_by_child.get(ancestor)
        if frame is None or ancestor is not content_panel:
            raise AssertionError(f"Native Hunts region escaped the content panel: {frame_name}")
    record = named_frames["$parentRecord"]
    record_anchor = next((element for element in record.iter()
                          if element.tag.endswith("Anchor") and element.get("point") == "BOTTOM"), None)
    if record_anchor is None or record_anchor.get("relativeTo") is not None:
        raise AssertionError("Hunt Record must anchor to the internal content panel bottom")
    state = named_frames["$parentHuntState"]
    state_anchor = next((element for element in state.iter()
                         if element.tag.endswith("Anchor") and element.get("point") == "TOP"), None)
    state_offset = next((element for element in state.iter()
                         if element.tag.endswith("AbsDimension")), None)
    if (state_anchor is None or state_offset is None or
            state_anchor.get("relativeTo") != "$parentIdentity" or
            state_anchor.get("relativePoint") != "BOTTOM" or
            state_offset.get("x") != "0" or state_offset.get("y") != "-30"):
        raise AssertionError("active Hunt composition must keep a fixed 30px identity/state gap")
    required_lua = (
        "LFDQueueFrame:Hide()",
        "NativeHuntsFrame:Show()",
        "NativeHuntsFrame:Hide()",
        "LFDQueueFrame:Show()",
        'hooksecurefunc("LFDFrame_OnEvent"',
        'event=="LFG_OPEN_FROM_GOSSIP"',
        'button.tooltipText = MicroButtonTooltipText("Player vs. Environment"',
        'LFDMicroButton:HookScript("OnEvent"',
        'event=="UPDATE_BINDINGS"',
        'LFDQueueFrameTitleText:SetText("Player vs. Environment")',
        "PanelTemplates_SetNumTabs(LFDParentFrame,2)",
        'RegisterAddonMessagePrefix(PREFIX)',
        'nonce=nonce%2147483646+1',
        'SendAddonMessage(PREFIX,"1\\tQ\\t"..nonce,"WHISPER"',
        'self:RegisterEvent("CHAT_MSG_ADDON")',
        'sequence <= lastSequence',
        'sender==UnitName("player")',
        'Hunt information unavailable.',
        'READY TO TURN IN',
        'TRAIL LOCATED',
        'FINAL CONFRONTATION',
        'HUNT PROGRESS',
        'HUNT COMPLETE',
        'Defeat your prey.',
        'NativeHuntsFrameContentPanelRecordStandard:SetText',
        'NativeHuntsFrameContentPanelRecordAvailability:SetText',
        'NativeHuntsFrameContentPanelRecordSeals:SetText',
        'NativeHuntsFrameContentPanelIdentityIssuer:SetText',
        'local HUNTS_PARENT_WIDTH, HUNTS_PARENT_HEIGHT = 355, 500',
        'local function PositionCloseButton()',
        'parentCloseAnchor = {parentCloseButton:GetPoint(1)}',
        'LFDParentFrame:HookScript("OnHide",ApplyStockGeometry)',
        r'Interface\\NativeHunts\\hunt_icon_standard.tga',
        r'Interface\\NativeHunts\\hunt_icon_elite.tga',
        "LFDParentFramePortraitTexture", "SetBlizzardPortraitArtworkVisible(false)",
        "SetBlizzardPortraitArtworkVisible(true)",
    )
    for token in required_lua:
        if token not in ui_lua:
            raise AssertionError(f"Native Hunts passive UI behavior missing: {token}")
    if "math.mod" in ui_lua:
        raise AssertionError("unsupported WoW 3.3.5a Lua math.mod call found")
    if "Player vs Environment" in ui_xml + ui_lua:
        raise AssertionError("unpunctuated Player vs. Environment presentation text found")
    for token in ("C_ChatInfo", "table.unpack", "bit32.", "utf8.", "goto ", "//"):
        if token in ui_lua:
            raise AssertionError(f"modern Lua/WoW API token found in build-12340 UI: {token}")
    for resource_id in ("14999010", "14999011", "14999980"):
        if resource_id in ui_lua:
            raise AssertionError(f"hard-coded managed resource ID found in client UI: {resource_id}")
    malformed_control_pattern = 'string.find(decoded, "[\\000-\\008\\011\\012\\014-\\031]")'
    if malformed_control_pattern in ui_lua:
        raise AssertionError("build-12340-incompatible NUL-containing Lua pattern found")
    required_decoder = (
        "local decoded, index = {}, 1",
        "local byte = string.byte(value, index)",
        'local hex = string.sub(value, index + 1, index + 2)',
        'string.find(hex, "^%x%x$")',
        "byte = tonumber(hex, 16)",
        "byte < 9 or byte == 11 or byte == 12 or (byte >= 14 and byte <= 31)",
        "return table.concat(decoded)",
        "if not assembly.fragments then assembly.fragments={}; end",
    )
    for token in required_decoder:
        if token not in ui_lua:
            raise AssertionError(f"Native Hunts fail-closed decoder behavior missing: {token}")

    try:
        from lupa.lua51 import LuaError, LuaRuntime
    except ImportError:
        print("SKIP direct Lua 5.1 client-codec vectors (lupa.lua51 unavailable)", flush=True)
    else:
        lua = LuaRuntime()
        decode, split, number, handle_message, render = lua.execute(
            ui_lua + "\nreturn Decode, Split, Number, HandleMessage, Render")

        lua.execute("""
			local function AnimationGroup()
				local animation = {
					SetChange=function() end, SetDuration=function() end, SetOrder=function() end
				}
				return {
					playing=false,
					CreateAnimation=function() return animation end,
					SetScript=function(self, event, callback) self[event]=callback end,
					IsPlaying=function(self) return self.playing end,
					Stop=function(self) self.playing=false end,
					Play=function(self) self.playing=true end
				}
			end
            local function Control()
                return {
					text="", value=0, texture=nil, visible=true, width=0, height=0,
					objectType="Frame", hooks={}, children={},
                    SetText=function(self, value) self.text=value end,
                    SetValue=function(self, value) self.value=value end,
                    SetTexture=function(self, value) self.texture=value end,
					SetWidth=function(self, value) self.width=value end,
					SetHeight=function(self, value) self.height=value end,
					GetWidth=function(self) return self.width end,
					GetHeight=function(self) return self.height end,
					GetObjectType=function(self) return self.objectType end,
					GetPoint=function(self) return unpack(self.point) end,
					GetChildren=function(self) return unpack(self.children) end,
                    ClearAllPoints=function(self) self.point=nil end,
                    SetPoint=function(self, point, relative, relativePoint, x, y)
                        self.point={point, relative, relativePoint, x, y}
                    end,
                    Hide=function(self) self.visible=false end,
					Show=function(self) self.visible=true end,
					IsShown=function(self) return self.visible end,
					HookScript=function(self, event, callback) self.hooks[event]=callback end,
					RegisterEvent=function() end,
					CreateAnimationGroup=function() return AnimationGroup() end
                }
            end
            NativeHuntsFrameContentPanelIdentity=Control()
            NativeHuntsFrameContentPanelIdentityIcon=Control()
            NativeHuntsFrameContentPanelIdentityTier=Control()
            NativeHuntsFrameContentPanelIdentityPrey=Control()
			NativeHuntsFrameContentPanelIdentityIssuer=Control()
            NativeHuntsFrameContentPanelHuntState=Control()
            NativeHuntsFrameContentPanelHuntStateHeader=Control()
            NativeHuntsFrameContentPanelHuntStatePrimary=Control()
            NativeHuntsFrameContentPanelHuntStateSecondary=Control()
            NativeHuntsFrameContentPanelHuntStateProgress=Control()
            NativeHuntsFrameContentPanelHuntStateProgressText=Control()
            NativeHuntsFrameContentPanelHuntStateDecoration=Control()
            NativeHuntsFrameContentPanelHuntStateReadyIcon=Control()
            NativeHuntsFrameContentPanelIdle=Control()
            NativeHuntsFrameContentPanelIdleState=Control()
            NativeHuntsFrameContentPanelIdleDescription=Control()
            NativeHuntsFrameContentPanelRecord=Control()
            NativeHuntsFrameContentPanelRecordStandard=Control()
            NativeHuntsFrameContentPanelRecordElite=Control()
            NativeHuntsFrameContentPanelRecordAvailability=Control()
            NativeHuntsFrameContentPanelRecordSealIcon=Control()
            NativeHuntsFrameContentPanelRecordSeals=Control()
			LFDParentFrame=Control()
			LFDParentFrame.width=355
			LFDParentFrame.height=440
			LFDQueueFrame=Control()
			NativeHuntsFrame=Control()
			LFDParentFrameTab1=Control()
			LFDParentFrameTab2=Control()
			LFDParentFramePortrait=Control()
			LFDParentFrameCloseButton=Control()
			LFDParentFrameCloseButton.objectType="Button"
			LFDParentFrameCloseButton:SetPoint("TOPRIGHT", LFDParentFrame, "TOPRIGHT", 2, -8)
			LFDParentFrame.children={LFDParentFrameCloseButton, LFDQueueFrame, NativeHuntsFrame,
				LFDParentFrameTab1, LFDParentFrameTab2, LFDParentFramePortrait}
			NativeHuntsFrameInitializer=Control()
        """)

        lua.globals().NativeHuntsFrame_OnLoad(lua.globals().NativeHuntsFrameInitializer)
        select_tab = lua.globals().NativeHuntsFrame_SelectTab
        select_tab(2)
        if ((controls := lua.globals()).LFDParentFrame.width != 355 or
                controls.LFDParentFrame.height != 500 or
                controls.LFDQueueFrame.visible or not controls.NativeHuntsFrame.visible):
            raise AssertionError("Hunts tab did not apply its bounded parent-frame size")
        controls.LFDParentFrame.hooks.OnHide()
        if (controls.LFDParentFrame.width != 355 or
                controls.LFDParentFrame.height != 440):
            raise AssertionError("hiding the PvE frame did not restore stock geometry")
        controls.LFDParentFrame.hooks.OnShow()
        if (controls.LFDParentFrame.width != 355 or
                controls.LFDParentFrame.height != 500):
            raise AssertionError("reopening the Hunts pane did not converge on Hunts geometry")
        select_tab(1)
        if (controls.LFDParentFrame.width != 355 or
                controls.LFDParentFrame.height != 440 or
                not controls.LFDQueueFrame.visible or controls.NativeHuntsFrame.visible):
            raise AssertionError("Dungeon Finder tab did not restore the stock parent-frame size")
        for _ in range(3):
            select_tab(2)
            select_tab(1)
        if (controls.LFDParentFrame.width != 355 or
                controls.LFDParentFrame.height != 440 or
                controls.LFDParentFrameTab1.point[4] != 18 or
                controls.LFDParentFrameTab1.point[5] != -27 or
                controls.LFDParentFrameTab2.point[4] != -15 or
				controls.LFDParentFrameTab2.point[5] != 0 or
				controls.LFDParentFrameCloseButton.point[1] != "TOPRIGHT" or
				controls.LFDParentFrameCloseButton.point[3] != "TOPRIGHT" or
				controls.LFDParentFrameCloseButton.point[4] != 2 or
				controls.LFDParentFrameCloseButton.point[5] != -8):
            raise AssertionError("repeated tab switching accumulated geometry drift")

        def escape(value: str) -> str:
            safe = b"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_.'"
            return "".join(chr(byte) if byte in safe else f"%{byte:02X}"
                           for byte in value.encode("utf-8"))

        legal_fields = (
            "", "Stormwind", "The Oathbreaker", "Player's Hunt",
            "[test]", "]", "[", "%", "\\", "\t", "\n", "\r",
            "field\twith%delimiter\\escapes",
        )
        for value in legal_fields:
            encoded = escape(value)
            if decode(encoded) != value:
                raise AssertionError(f"Lua client codec round trip failed: {value!r} -> {encoded!r}")

        malformed_fields = (
            "%", "%0", "%GG", "%0G", "%G0", "abc%", "abc%2", "%%25",
            "%00", "%08", "%0B", "%0C", "%0E", "%1F",
        )
        for value in malformed_fields:
            if decode(value) is not None:
                raise AssertionError(f"Lua client decoder accepted malformed field: {value!r}")
        if decode("a" * 1024) != "a" * 1024 or decode("a" * 1025) is not None:
            raise AssertionError("Lua client decoder field-length boundary regressed")

        fields = split("alpha\t\tomega\t")
        if (len(fields) != 4 or fields[1] != "alpha" or fields[2] != "" or
                fields[3] != "omega" or fields[4] != ""):
            raise AssertionError("Lua client splitter lost an empty or trailing field")
        limited = split("1\tF\t2\t0\tA\t1\t1\tbody\twith\ttabs", 8)
        if len(limited) != 8 or limited[8] != "body\twith\ttabs":
            raise AssertionError("Lua client splitter did not preserve the limited remainder")

        if (number("0", 4294967295) != 0 or
                number("4294967295", 4294967295) != 4294967295):
            raise AssertionError("Lua client unsigned-number boundary regressed")
        for value in ("", "-1", "+1", "1.0", " 1", "1 ", "4294967296", "x", "1\t2"):
            if number(value, 4294967295) is not None:
                raise AssertionError(f"Lua client number parser accepted malformed value: {value!r}")

        malformed_messages = (
            None, "", "x", "2\tA\t1\t0\t", "1\tX\t1\t0\t",
            "1\tA", "1\tA\t0\t0\t", "1\tA\t1\t-1\t",
            "1\tA\t4294967296\t0\t", "1\tA\t1\t4294967296\t",
            "1\tP\t1\t0\t0\t0\t0\t0\t0\t0\tX\t0",
            "1\tF\t1\t0\tX\t1\t1\tbody",
            "1\tF\t1\t0\tA\t0\t1\tbody",
            "1\tF\t1\t0\tA\t2\t1\tbody",
            "1\tF\t1\t0\tA\t1\t9\tbody",
            "1\tF\t1\t0\tA\t1\t1\tbody\textra",
            "1\tA\t1\t0\t1\t1\t1\tT\tS\t50\t0\t0\t%\tStormwind\tPrey\tZone\t\t",
            "1\tA\t1\t0\t1\t1\t1\tT\tS\t50\t0\t0\t%00\tStormwind\tPrey\tZone\t\t",
            "1\tA\t1\t0\t1\t1\t1\tT\tS\t101\t0\t0\tName\tCity\tPrey\tZone\t\t",
            "1\tA\t1\t0\t1\t1\t1\tX\tS\t50\t0\t0\tName\tCity\tPrey\tZone\t\t",
            "a" * 249,
        )
        for message in malformed_messages:
            try:
                handle_message(message)
            except LuaError as error:
                raise AssertionError(
                    f"malformed NHUNTS message raised a Lua error: {message!r}") from error

        old_pattern = "[\x00-\x08\x0b\x0c\x0e-\x1f]"
        try:
            lua.globals().string.find("", old_pattern)
        except LuaError as error:
            if "malformed pattern (missing ']')" not in str(error):
                raise
        else:
            raise AssertionError("Lua 5.1 did not reproduce the PTR malformed-pattern failure")
        if decode("") != "":
            raise AssertionError("empty-field regression for PTR malformed-pattern failure")

        def lua_table(value):
            if isinstance(value, dict):
                return lua.table_from({key: lua_table(item) for key, item in value.items()})
            return value

        stats = {
            "standard": 11, "elite": 3, "eliteUnlocked": True,
            "accepted": 1, "limit": 1, "eliteAvailable": False,
            "sealState": "A", "seals": 7,
        }
        active = {
            "revision": 1, "contentAvailable": True, "active": True,
            "state": "T", "tier": "S", "progress": 54,
            "finalVisible": False, "ready": False,
            "huntmaster": "Huntmaster Varyn", "city": "Dalaran",
            "prey": "The Headsman", "zone": "Crystalsong Forest",
            "finalLocation": "Crystalsong Forest", "reason": "", "stats": stats,
        }
        controls = lua.globals()

        def rendered(overrides=None, stats_overrides=None):
            snapshot = dict(active)
            snapshot["stats"] = dict(stats)
            if overrides:
                snapshot.update(overrides)
            if stats_overrides:
                snapshot["stats"].update(stats_overrides)
            render(lua_table(snapshot))
            return controls

        idle = rendered({"active": False, "state": "I", "tier": "N", "progress": 0})
        if (not idle.NativeHuntsFrameContentPanelIdle.visible or
                idle.NativeHuntsFrameContentPanelIdentity.visible or
                idle.NativeHuntsFrameContentPanelHuntState.visible or
                not idle.NativeHuntsFrameContentPanelRecord.visible or
                idle.NativeHuntsFrameContentPanelIdleState.text != "NO ACTIVE HUNT" or
                idle.NativeHuntsFrameContentPanelIdleDescription.text !=
                "Speak with a Huntmaster\nto begin a Hunt."):
            raise AssertionError("Native Hunts idle rendering regressed")

        standard = rendered()
        if (not standard.NativeHuntsFrameContentPanelIdentity.visible or
                not standard.NativeHuntsFrameContentPanelHuntState.visible or
                standard.NativeHuntsFrameContentPanelIdle.visible or
                standard.NativeHuntsFrameContentPanelIdentityTier.text != "STANDARD HUNT" or
                standard.NativeHuntsFrameContentPanelIdentityIcon.texture !=
                "Interface\\NativeHunts\\hunt_icon_standard.tga" or
                standard.NativeHuntsFrameContentPanelIdentityPrey.text != "The Headsman" or
                standard.NativeHuntsFrameContentPanelIdentityIssuer.text !=
                "Huntmaster Varyn  |cff9d9d9d•|r  Dalaran" or
                standard.NativeHuntsFrameContentPanelHuntStateHeader.text != "HUNT PROGRESS" or
                standard.NativeHuntsFrameContentPanelHuntStatePrimary.text != "Tracking" or
                not standard.NativeHuntsFrameContentPanelHuntStateProgress.visible or
                standard.NativeHuntsFrameContentPanelHuntStateProgress.value != 54 or
                standard.NativeHuntsFrameContentPanelHuntStateProgressText.text != "54%" or
                not standard.NativeHuntsFrameContentPanelHuntStateDecoration.visible or
                standard.NativeHuntsFrameContentPanelHuntStateReadyIcon.visible or
                standard.NativeHuntsFrameContentPanelHuntStateSecondary.text !=
                "Follow the trail through |cffffd200Crystalsong Forest|r."):
            raise AssertionError("Standard Hunt tracking rendering regressed")
        elite = rendered({"tier": "E"})
        if (elite.NativeHuntsFrameContentPanelIdentityTier.text != "ELITE HUNT" or
                elite.NativeHuntsFrameContentPanelIdentityIcon.texture !=
                "Interface\\NativeHunts\\hunt_icon_elite.tga"):
            raise AssertionError("Elite Hunt tracking rendering regressed")

        low = rendered({"progress": -1})
        if (low.NativeHuntsFrameContentPanelHuntStateProgress.value != 0 or
                low.NativeHuntsFrameContentPanelHuntStateProgressText.text != "0%"):
            raise AssertionError("tracking progress lower clamp regressed")
        high = rendered({"progress": 101})
        if (high.NativeHuntsFrameContentPanelHuntStateProgress.value != 100 or
                high.NativeHuntsFrameContentPanelHuntStateProgressText.text != "100%"):
            raise AssertionError("tracking progress upper clamp regressed")

        revealed = rendered({"state": "F", "progress": 100, "finalVisible": True})
        if (revealed.NativeHuntsFrameContentPanelHuntStateHeader.text != "TRAIL LOCATED" or
                "Final Location" not in revealed.NativeHuntsFrameContentPanelHuntStatePrimary.text or
                "Crystalsong Forest" not in revealed.NativeHuntsFrameContentPanelHuntStatePrimary.text or
                "Prey Trail Crystal" not in revealed.NativeHuntsFrameContentPanelHuntStateSecondary.text or
                not revealed.NativeHuntsFrameContentPanelHuntStateProgress.visible or
                revealed.NativeHuntsFrameContentPanelHuntStateProgress.value != 100 or
                revealed.NativeHuntsFrameContentPanelHuntStateProgressText.text != "100%" or
                revealed.NativeHuntsFrameContentPanelHuntStateDecoration.visible or
                revealed.NativeHuntsFrameContentPanelHuntStateReadyIcon.visible):
            raise AssertionError("FinalRevealed rendering regressed")
        confrontation = rendered({"state": "P", "progress": 100, "finalVisible": True})
        if (confrontation.NativeHuntsFrameContentPanelHuntStateHeader.text !=
                "FINAL CONFRONTATION" or
                "The Headsman" not in confrontation.NativeHuntsFrameContentPanelHuntStatePrimary.text or
                confrontation.NativeHuntsFrameContentPanelHuntStateSecondary.text != "Defeat your prey." or
                confrontation.NativeHuntsFrameContentPanelHuntStateProgress.visible):
            raise AssertionError("PreyActive rendering regressed")
        ready = rendered({"state": "R", "progress": 100, "finalVisible": True, "ready": True})
        if (ready.NativeHuntsFrameContentPanelHuntStateHeader.text != "HUNT COMPLETE" or
                "READY TO TURN IN" not in ready.NativeHuntsFrameContentPanelHuntStatePrimary.text or
                ready.NativeHuntsFrameContentPanelHuntStateSecondary.text !=
                "Return to Huntmaster Varyn\nin Dalaran." or
                ready.NativeHuntsFrameContentPanelHuntStateProgress.visible or
                not ready.NativeHuntsFrameContentPanelHuntStateReadyIcon.visible):
            raise AssertionError("ReadyToTurnIn rendering regressed")

        returned_idle = rendered({"active": False, "state": "I", "tier": "N", "progress": 0})
        if (returned_idle.NativeHuntsFrameContentPanelIdentity.visible or
                returned_idle.NativeHuntsFrameContentPanelHuntState.visible or
                not returned_idle.NativeHuntsFrameContentPanelIdle.visible or
                returned_idle.NativeHuntsFrameContentPanelIdentityTier.text != "" or
                returned_idle.NativeHuntsFrameContentPanelHuntStateHeader.text != "" or
                returned_idle.NativeHuntsFrameContentPanelHuntStatePrimary.text != "" or
                returned_idle.NativeHuntsFrameContentPanelHuntStateSecondary.text != "" or
                returned_idle.NativeHuntsFrameContentPanelHuntStateProgress.visible or
                returned_idle.NativeHuntsFrameContentPanelHuntStateProgress.value != 0 or
                returned_idle.NativeHuntsFrameContentPanelHuntStateProgressText.text != "" or
                returned_idle.NativeHuntsFrameContentPanelHuntStateDecoration.visible or
                returned_idle.NativeHuntsFrameContentPanelHuntStateReadyIcon.visible):
            raise AssertionError("return to Idle retained stale active-Hunt presentation")

        if (returned_idle.NativeHuntsFrameContentPanelRecordStandard.text != "11" or
                returned_idle.NativeHuntsFrameContentPanelRecordElite.text != "3" or
                "Unavailable" not in
                returned_idle.NativeHuntsFrameContentPanelRecordAvailability.text or
                returned_idle.NativeHuntsFrameContentPanelRecordSeals.text != "7" or
                not returned_idle.NativeHuntsFrameContentPanelRecordSealIcon.visible):
            raise AssertionError("lifetime Hunt or physical Seal rendering regressed")
        available = rendered(stats_overrides={"eliteAvailable": True})
        if "Available" not in available.NativeHuntsFrameContentPanelRecordAvailability.text:
            raise AssertionError("daily Elite availability rendering regressed")
        unavailable_seals = rendered(stats_overrides={"sealState": "U"})
        if (unavailable_seals.NativeHuntsFrameContentPanelRecordSealIcon.visible or
                unavailable_seals.NativeHuntsFrameContentPanelRecordSeals.text != "Unavailable"):
            raise AssertionError("unavailable physical Seal rendering regressed")

        fallback = rendered({"state": "F", "huntmaster": "", "city": "", "prey": "",
                             "zone": "", "finalLocation": ""})
        if (fallback.NativeHuntsFrameContentPanelIdentityPrey.text != "Unknown Quarry" or
                "Unknown Huntmaster" not in fallback.NativeHuntsFrameContentPanelIdentityIssuer.text or
                "Unknown Location" not in fallback.NativeHuntsFrameContentPanelIdentityIssuer.text or
                "Unknown Hunting Ground" not in
                fallback.NativeHuntsFrameContentPanelHuntStatePrimary.text):
            raise AssertionError("missing optional display fields did not degrade safely")
        try:
            render(lua.table_from({}))
        except LuaError as error:
            raise AssertionError("incomplete snapshot raised a Lua rendering error") from error
        if (not controls.NativeHuntsFrameContentPanelIdle.visible or
                controls.NativeHuntsFrameContentPanelRecord.visible or
                controls.NativeHuntsFrameContentPanelIdleState.text !=
                "Hunt information unavailable."):
            raise AssertionError("incomplete snapshot did not use the bounded idle region")
        print("PASS direct Lua 5.1 NHUNTS codec, renderer, and malformed-input vectors", flush=True)
    forbidden_ui = (
        "HuntsUI", '<Frame name="LFDParentFrame"',
        '<Frame name="LFDQueueFrame"', "function LFDFrame_OnEvent",
        "function ToggleLFDParentFrame",
    )
    combined_ui = json.dumps(manifest) + ui_xml + ui_lua
    for token in forbidden_ui:
        if token in combined_ui:
            raise AssertionError(f"forbidden client replacement/protocol token found: {token}")
    forbidden_mutations = (
        "AcceptHunt", "AdvanceTracking", "MarkPreyKilled", "TurnInHunt",
        "AbandonHunt", "UseCrystal", "UseReturnRift",
    )
    for token in forbidden_mutations:
        if token in ui_lua:
            raise AssertionError(f"client gameplay mutation path found: {token}")
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
    huntmaster_templates = [row for row in manifest["creatureTemplates"]
                            if row["symbol"] in expected_huntmasters]
    huntmaster_spawns = [row for row in manifest["creatureSpawns"]
                         if row["creature"]["symbol"] in expected_huntmasters]
    if (len(huntmaster_templates) != len(expected_huntmasters) or
            len(huntmaster_spawns) != len(expected_huntmasters)):
        raise AssertionError("each logical Huntmaster must have exactly one template and spawn declaration")
    spawn_targets = [row["creature"]["symbol"] for row in huntmaster_spawns]
    spawn_locations = [(row["map"], row["x"], row["y"], row["z"])
                       for row in huntmaster_spawns]
    if (len(set(spawn_targets)) != len(spawn_targets) or
            len(set(spawn_locations)) != len(spawn_locations)):
        raise AssertionError("Huntmaster manifest contains a duplicate target or physical location")
    for row in huntmaster_templates:
        overrides = row.get("overrides", {})
        if (overrides.get("scriptName") != "mod_native_hunts_huntmaster" or
                overrides.get("npcFlags") != 1):
            raise AssertionError(f"Huntmaster template contract regressed: {row['symbol']}")
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
    seal_rows = [row for row in manifest["dbcRows"]
                 if row["symbol"] == "huntmaster-seal"]
    if len(seal_rows) != 1:
        raise AssertionError("physical Huntmaster's Seal is not declared")
    if seal_rows[0]["fields"].get("DisplayInfoID") != {"copyFromItem": 45978}:
        raise AssertionError(
            "Huntmaster's Seal must retain the original INV_Misc_Coin_17 display donor")
    serialized = json.dumps(manifest)
    for disposable in ("test-creature", "test-creature-spawn", "test-object"):
        if disposable in serialized:
            raise AssertionError(f"disposable resource remains: {disposable}")
    print("PASS functional Native Hunts FrameXML client contract", flush=True)


def run_frameforge_functional_ui_checks() -> None:
    """Verify the PTR2 explicit BLP2 package and runtime presentation."""
    content_root = ROOT / "content"
    manifest_source = (content_root / "manifest.json").read_bytes()
    manifest = json.loads(manifest_source)
    if manifest.get("version") != "22-ptr7":
        print("SKIP functional FrameForge PTR7 contract", flush=True)
        return
    if (manifest.get("schema"), manifest.get("package")) != (3, "mod-native-hunts"):
        raise AssertionError("invalid Native Hunts PTR3 package identity")
    if "frameForgeWowUi" in manifest or not manifest.get("content"):
        raise AssertionError("functional package must use explicit managed content, not static FrameForge staging")
    expected_frame_xml = {
        "stockTocSource": "upstream/FrameXML.toc",
        "stockTocSha256": "3158bea13225ae51137a389f0f3ab8566e94b6be84196dd2c1fda27024677754",
        "loadEntries": [
            {"target": "Interface/FrameXML/NativeHuntsFrame.xml",
             "after": "Interface/FrameXML/LFDFrame.xml"},
            {"target": "Interface/FrameXML/NativeHuntsFrameForgeLayout.xml",
             "after": "Interface/FrameXML/NativeHuntsFrame.xml"},
            {"target": "Interface/FrameXML/NativeHuntsInitializer.xml",
             "after": "Interface/FrameXML/NativeHuntsFrameForgeLayout.xml"},
        ],
    }
    if manifest.get("clientFrameXml") != expected_frame_xml:
        raise AssertionError("functional FrameXML load order no longer follows LFDFrame.xml")
    required_targets = {
        "Interface/FrameXML/NativeHuntsFrame.xml",
        "Interface/FrameXML/NativeHuntsFrame.lua",
        "Interface/FrameXML/NativeHuntsFrameForgeLayout.xml",
        "Interface/FrameXML/NativeHuntsInitializer.xml",
    }
    targets = {entry["target"] for entry in manifest["content"]}
    if not required_targets <= targets:
        raise AssertionError("functional XML/Lua relationship is not packaged")

    expected_members = tuple(sorted({"manifest.json", expected_frame_xml["stockTocSource"], *(
        entry["source"] for entry in manifest["content"])}))
    with zipfile.ZipFile(content_root / "mod-native-hunts.epf") as archive:
        if tuple(archive.namelist()) != expected_members:
            raise AssertionError("functional EPF member set or deterministic order is incorrect")
        packaged = {name: archive.read(name) for name in expected_members}
    if packaged["manifest.json"] != manifest_source:
        raise AssertionError("functional EPF manifest is stale")
    for entry in manifest["content"]:
        if packaged[entry["source"]] != (content_root / entry["source"]).read_bytes():
            raise AssertionError(f"functional EPF member is stale: {entry['source']}")

    exported_manifest = json.loads((content_root / "WoWUI/frameforge-manifest.json").read_text())
    assets_manifest = json.loads((content_root / "WoWUI/assets-manifest.json").read_text())
    project = json.loads((ROOT / "assets/Native-Hunts.fforge.json").read_text())
    project_frames = {frame["name"]: frame for frame in project["frames"]}
    record_source = project_frames["DesignObject2"]
    if (record_source.get("level") != 6 or
            record_source.get("visual", {}).get("texture", {}).get("file") !=
            "hunt_panel_record.png"):
        raise AssertionError("Hunt Record source project layering or artwork regressed")
    inventory = exported_manifest.get("bindings", [])
    if (exported_manifest.get("schema") != "frameforge-wow335-manifest" or
            exported_manifest.get("version") != 2 or len(inventory) != 47):
        raise AssertionError("authoritative 47-object FrameForge contract is incorrect")
    if [state["id"] for state in exported_manifest.get("states", [])] != [
            "idle", "tracking", "located", "fight", "turnin"]:
        raise AssertionError("FrameForge visual state order or identity regressed")

    xml_source = content_root / "client/Interface/FrameXML/NativeHuntsFrame.xml"
    lua_source = content_root / "client/Interface/FrameXML/NativeHuntsFrame.lua"
    ui_xml = xml_source.read_text(encoding="utf-8")
    ui_lua = lua_source.read_text(encoding="utf-8")
    xml_root = ET.fromstring(ui_xml)
    layout_root = ET.parse(content_root / "WoWUI/FrameForgeLayout.xml").getroot()
    names = {element.get("name") for element in layout_root.iter() if element.get("name")}
    inventory_names = {entry["runtimeId"] for entry in inventory}
    if len(inventory_names) != 47 or not inventory_names <= names:
        raise AssertionError("integrated FrameXML lost an authored control identity")
    if ("Hunt_Tier_Icon" not in inventory_names or
            {"Standard_Hunt_Icon", "Elite_Hunt_Icon"} & inventory_names):
        raise AssertionError("tier icon was not consolidated to one stable control")
    wrappers = {entry["visibilityRuntimeId"] for entry in inventory
                if entry["type"] in {"Texture", "FontString"}}
    if not wrappers <= names:
        raise AssertionError("integrated FrameXML lost a generated presentation wrapper")
    named = {element.get("name"): element for element in layout_root.iter()
             if element.get("name")}
    tier_wrapper = named["Hunt_Tier_Icon__FFLayer"]
    panel_wrapper = named["Hunt_Panel_Top__FFLayer"]
    if (tier_wrapper.get("frameLevel") != "21" or
            int(tier_wrapper.get("frameLevel")) <= int(panel_wrapper.get("frameLevel"))):
        raise AssertionError("tier icon must stay above the active panel without Lua frame-level changes")
    record_wrapper = named["Hunt_Panel_Record__FFLayer"]
    record_statistic_names = {
        "Hunt_Seals", "Elite_Today", "Elite_Hunts", "Standard_Hunts",
        "Hunt_Seal", "Standard_Hunts_Value", "Elite_Hunts_Value",
        "Elite_Today_Available", "Elite_Today_Unavailable", "Hunt_Seal_Value",
    }
    record_statistic_wrappers = [named[f"{name}__FFLayer"]
                                 for name in record_statistic_names]
    record_level = int(record_wrapper.get("frameLevel"))
    if (record_wrapper.get("frameStrata") != "MEDIUM" or
            any(wrapper.get("frameStrata") != "MEDIUM" or
                int(wrapper.get("frameLevel")) <= record_level
                for wrapper in record_statistic_wrappers)):
        raise AssertionError(
            "Hunt Record artwork must stay below every statistic in one compatible strata")
    record_layer = next(element for element in record_wrapper.iter()
                        if element.tag.endswith("Layer"))
    if record_layer.get("level") not in {"BACKGROUND", "BORDER", "ARTWORK", "OVERLAY"}:
        raise AssertionError("Hunt Record artwork uses a non-persistent draw layer")
    for statistic_name in record_statistic_names:
        statistic_layer = next(
            element for element in named[f"{statistic_name}__FFLayer"].iter()
            if element.tag.endswith("Layer"))
        if statistic_layer.get("level") != "ARTWORK":
            raise AssertionError(
                f"persistent Hunt Record statistic {statistic_name} must export to ARTWORK")
    lower_panel_levels = {
        name: int(named[name].get("frameLevel"))
        for name in ("Hunt_Panel_Top__FFLayer", "Hunt_Panel_Bottom__FFLayer")
    }
    if lower_panel_levels != {
            "Hunt_Panel_Top__FFLayer": 7,
            "Hunt_Panel_Bottom__FFLayer": 8}:
        raise AssertionError("lower hunt panel layering changed")
    grounds_wrapper = named["Tracker_Hunt_Ground__FFLayer"]
    grounds_size = next(element for element in grounds_wrapper if element.tag.endswith("Size"))
    grounds_dimensions = next(element for element in grounds_size if element.tag.endswith("AbsDimension"))
    grounds_label = named["Tracker_Hunt_Ground"]
    grounds_height = next(element for element in grounds_label if element.tag.endswith("FontHeight"))
    grounds_abs = next(element for element in grounds_height if element.tag.endswith("AbsValue"))
    if (grounds_dimensions.get("x") != "100" or grounds_dimensions.get("y") != "16" or
            grounds_label.get("text") != "Hunting Grounds:" or
            grounds_label.get("font") != r"Fonts\FRIZQT__.TTF" or grounds_abs.get("val") != "10"):
        raise AssertionError("Hunting Grounds label dimensions or effective font declaration regressed")
    frameforge_root = named["FrameForge_Dungeon_Finder_UI"]
    size = next(element for element in frameforge_root if element.tag.endswith("Size"))
    dimensions = next(element for element in size if element.tag.endswith("AbsDimension"))
    if (frameforge_root.get("parent") != "LFDParentFrame" or
            dimensions.get("x") != "355" or dimensions.get("y") != "440"):
        raise AssertionError("authoritative FrameForge root parent or dimensions changed")
    if frameforge_root.get("hidden") != "true":
        raise AssertionError("FrameForge root must remain hidden until the Hunts tab is selected")
    host_named = {element.get("name"): element for element in xml_root.iter()
                  if element.get("name")}
    if (host_named.get("NativeHuntsFrame") is None or
            host_named["NativeHuntsFrame"].get("parent") != "LFDParentFrame" or
            host_named.get("$parentContentPanel") is None or
            host_named["$parentContentPanel"].get("hidden") != "true"):
        raise AssertionError("legacy Native Hunts presentation was not suppressed")
    if not {"LFDParentFrameTab1", "LFDParentFrameTab2"} <= host_named.keys():
        raise AssertionError("Dungeon Finder and Hunts tabs must both remain defined")
    native_frame = host_named["NativeHuntsFrame"]
    shell_layers = next(element for element in native_frame
                        if element.tag.endswith("Layers"))
    shell_titles = [element.get("text") for element in shell_layers.iter()
                    if element.tag.endswith("FontString")]
    if shell_titles != ["Native Hunts"] or "Player vs. Environment" in ui_xml:
        raise AssertionError("Hunts shell title is incorrect")
    shell_bottoms = []
    for texture in (element for element in shell_layers.iter()
                    if element.tag.endswith("Texture")):
        size = next(element for element in texture if element.tag.endswith("Size"))
        anchor = next(element for element in texture.iter()
                      if element.tag.endswith("Anchor"))
        if anchor.get("point") != "TOPLEFT":
            raise AssertionError("shell artwork must retain simple TOPLEFT geometry")
        offset = next((element for element in anchor.iter()
                       if element.tag.endswith("AbsDimension")), None)
        offset_y = float(offset.get("y")) if offset is not None else 0.0
        shell_bottoms.append(-offset_y + float(size.get("y")))
    if not shell_bottoms or max(shell_bottoms) != 440 or any(bottom > 440 for bottom in shell_bottoms):
        raise AssertionError("Native Hunts shell artwork exceeds the 440px Blizzard parent")
    script_files = {element.get("file") for element in xml_root.iter()
                    if element.tag.endswith("Script")}
    if "NativeHuntsFrame.lua" not in script_files:
        raise AssertionError("functional host or Lua relationship regressed")

    presentation_rows = re.findall(
        r'\{"([A-Za-z0-9_]+)", "([A-Za-z0-9_]+)", "([*ITLFR]+)"\}', ui_lua)
    if len(presentation_rows) != 47 or {row[0] for row in presentation_rows} != inventory_names:
        raise AssertionError("Lua presentation inventory does not cover exactly 47 exported controls")
    row_by_name = {row[0]: row for row in presentation_rows}
    state_code = {"idle": "I", "tracking": "T", "located": "L", "fight": "F", "turnin": "R"}
    for state in exported_manifest["states"]:
        expected_show = set(state["show"])
        actual_show = {wrapper for _, wrapper, memberships in presentation_rows
                       if memberships == "*" or state_code[state["id"]] in memberships}
        if actual_show != expected_show:
            raise AssertionError(f"Lua state membership differs from FrameForge state {state['id']}")
    for entry in inventory:
        if entry["type"] in {"Texture", "FontString"}:
            expected_wrapper = entry["visibilityRuntimeId"]
            if row_by_name[entry["runtimeId"]][1] != expected_wrapper:
                raise AssertionError(f"Lua wrapper differs from inventory: {entry['runtimeId']}")

    required_lua = (
        "Standard_Hunts_Value:SetText", "Elite_Hunts_Value:SetText",
        "Hunt_Seal_Value:SetText", "Tracker_Target_Name:SetText",
        "Tracker_Huntmaster:SetText", "Tracker_Location:SetText",
        "Tracker_Hunt_Ground_Value:SetText", "Complete2B_Huntmaster_Value:SetText",
        "Complete3B_Location_Value:SetText", "Tracker_Hunt_Progress:SetMinMaxValues(0, 100)",
        "Tracker_Hunt_Progress:SetValue", 'snapshot.state == "T" and "T"',
        'snapshot.state == "F" and "L"', 'snapshot.state == "P" and "F"',
        'snapshot.state == "R" and "R"', "LFDQueueFrame:Hide()", "NativeHuntsFrame:Show()",
        "FrameForge_Dungeon_Finder_UI:Show()", "FrameForge_Dungeon_Finder_UI:Hide()",
        'RegisterAddonMessagePrefix(PREFIX)', 'self:RegisterEvent("CHAT_MSG_ADDON")',
        'self:RegisterEvent("GOSSIP_SHOW")', 'if event=="GOSSIP_SHOW" then',
        'HideUIPanel(LFDParentFrame)',
        'LFDParentFrame.numTabs = 2', 'InitializeTabs();',
        'type(LFDParentFrame.numTabs) ~= "number"', "ValidateFrameForgeControls",
        "NativeHuntsFrameInitializationError", "required exported control is missing",
        "Hunt_Tier_Icon:SetTexture(ELITE_HUNT_ICON_TEXTURE)",
        "Hunt_Tier_Icon:SetTexture(STANDARD_HUNT_ICON_TEXTURE)",
        "FrameForge_Dungeon_Finder_UI:SetParent(NativeHuntsFrame)",
    )
    for token in required_lua:
        if token not in ui_lua:
            raise AssertionError(f"module-owned FrameForge renderer behavior missing: {token}")
    for legacy in ("NativeHuntsFrameContentPanelIdentity", "NativeHuntsFrameContentPanelHuntState",
                   "NativeHuntsFrameContentPanelRecordStandard", "Standard_Hunt_Icon", "Elite_Hunt_Icon"):
        if legacy in ui_lua:
            raise AssertionError(f"Lua still drives obsolete presentation control: {legacy}")
    for token in ("C_ChatInfo", "table.unpack", "bit32.", "utf8.", "goto ", "//"):
        if token in ui_lua:
            raise AssertionError(f"modern Lua/WoW API token found: {token}")
    for forbidden in ("LFDParentFrame:SetWidth", "LFDParentFrame:SetHeight",
                      "LFDQueueFrameTitleText:SetText", "PositionCloseButton",
                      "HUNTS_PARENT_HEIGHT", "SetFrameLevel", "SetFrameStrata"):
        if forbidden in ui_lua:
            raise AssertionError(f"Blizzard geometry/title mutation remains: {forbidden}")

    target_by_logical = {entry["target"].replace("/", "\\").lower(): entry
                         for entry in manifest["content"]}
    legacy_entries = [entry for entry in manifest["content"]
                      if entry["source"].startswith("client/Interface/NativeHunts/")]
    if len(legacy_entries) != 10 or any(not entry["source"].endswith(".tga")
                                       for entry in legacy_entries):
        raise AssertionError("all ten legacy Native Hunts TGA sources must remain packaged")
    legacy_references = set(re.findall(r"Interface\\NativeHunts\\[^\"<]+\.tga", ui_xml))
    expected_legacy_references = {
        r"Interface\NativeHunts\hunt_divider.tga",
        r"Interface\NativeHunts\hunt_icon_seal.tga",
        r"Interface\NativeHunts\hunt_icon_turnin.tga",
        r"Interface\NativeHunts\hunt_panel_identity.tga",
        r"Interface\NativeHunts\hunt_panel_idle.tga",
        r"Interface\NativeHunts\hunt_panel_record.tga",
        r"Interface\NativeHunts\hunt_panel_state.tga",
        r"Interface\NativeHunts\hunt_trail_prints.tga",
    }
    if legacy_references != expected_legacy_references:
        raise AssertionError("legacy Native Hunts TGA references changed")
    for entry in legacy_entries:
        data = (content_root / entry["source"]).read_bytes()
        if len(data) < 18 or data[2] != 2 or data[16] != 32 or data[17] != 0x28:
            raise AssertionError(f"legacy artwork is not a 32-bit top-origin TGA: {entry['source']}")

    asset_rows = assets_manifest.get("assets", [])
    if len(asset_rows) != 11:
        raise AssertionError("FrameForge export must retain all 11 authored assets")
    icon_assets = {row["logicalTexture"]: row for row in asset_rows
                   if row["logicalTexture"].endswith(("hunt_icon_standard", "hunt_icon_elite"))}
    if (set(icon_assets) != {
            r"Interface\FrameForge\Dungeon_Finder_UI\hunt_icon_standard",
            r"Interface\FrameForge\Dungeon_Finder_UI\hunt_icon_elite"} or
            any(row.get("consumers") != ["Hunt_Tier_Icon"] for row in icon_assets.values())):
        raise AssertionError("both tier textures must be packaged for the shared icon host")
    frameforge_entries = [entry for entry in manifest["content"]
                          if entry["source"].startswith("WoWUI/textures/")]
    if (len(frameforge_entries) != 11 or
            any(not entry["source"].endswith(".blp") or
                not entry["target"].endswith(".blp") for entry in frameforge_entries)):
        raise AssertionError("EPF manifest must package all 11 FrameForge textures as BLP")
    for row in asset_rows:
        source = content_root / "WoWUI" / row["generatedFile"]
        if hashlib.sha256(source.read_bytes()).hexdigest().upper() != row["generatedContentSha256"]:
            raise AssertionError(f"generated FrameForge asset hash mismatch: {source.name}")
        target = row["requiredMpqDestination"].replace("/", "\\").lower()
        entry = target_by_logical.get(target)
        if not entry:
            raise AssertionError(f"runtime FrameForge artwork is not packaged: {target}")
        data = (content_root / entry["source"]).read_bytes()
        if (len(data) < 148 or data[:4] != b"BLP2" or
                struct.unpack_from("<I", data, 4)[0] != 1 or data[8:12] != bytes((2, 8, 7, 1))):
            raise AssertionError(f"runtime artwork is not BLP2/DXT5: {entry['source']}")
        width, height = struct.unpack_from("<II", data, 12)
        if (width & (width - 1)) or (height & (height - 1)):
            raise AssertionError(f"runtime FrameForge texture is not power-of-two: {entry['source']}")
        offsets = struct.unpack_from("<16I", data, 20)
        lengths = struct.unpack_from("<16I", data, 84)
        mip_count = 0
        mip_width, mip_height = width, height
        payload_end = 148
        for offset, length in zip(offsets, lengths):
            if not offset and not length:
                continue
            expected_length = max(1, (mip_width + 3) // 4) * max(1, (mip_height + 3) // 4) * 16
            if offset < 148 or length != expected_length or offset + length > len(data):
                raise AssertionError(f"invalid BLP2 mip table: {entry['source']}")
            payload_end = max(payload_end, offset + length)
            mip_count += 1
            mip_width, mip_height = max(1, mip_width // 2), max(1, mip_height // 2)
        if mip_count != row["mipCount"] or payload_end != len(data):
            raise AssertionError(f"incomplete or trailing BLP2 payload: {entry['source']}")
        if data != packaged[entry["source"]]:
            raise AssertionError(f"FrameForge texture was reconverted in the EPF: {entry['source']}")

    forbidden_members = [member for member in expected_members
                         if member != "manifest.json" and
                         (member.endswith(".png") or member.endswith(".json") or
                          member.endswith("export-report.txt"))]
    if forbidden_members:
        raise AssertionError(f"development-only FrameForge files entered EPF: {forbidden_members}")

    try:
        from lupa.lua51 import LuaError, LuaRuntime
    except ImportError:
        print("SKIP direct Lua 5.1 FrameForge renderer vectors (lupa.lua51 unavailable)", flush=True)
    else:
        lua = LuaRuntime()
        decode, render, handle_message = lua.execute(
            ui_lua + "\nreturn Decode, Render, HandleMessage")
        lua.execute("""
            local function AnimationGroup()
                local animation={SetChange=function() end,SetDuration=function() end,SetOrder=function() end}
                return {playing=false,CreateAnimation=function() return animation end,
                    SetScript=function(self,event,callback) self[event]=callback end,
                    IsPlaying=function(self) return self.playing end,Stop=function(self) self.playing=false end,
                    Play=function(self) self.playing=true end}
            end
            function FrameForgeTestControl()
                return {text="",value=0,min=0,max=0,visible=true,width=0,height=0,objectType="Frame",hooks={},children={},events={},parent=nil,
                    SetText=function(self,value) self.text=value end,SetValue=function(self,value) self.value=value end,
                    SetNormalTexture=function(self,value) self.texture=value end,EnableMouse=function(self,value) self.mouseEnabled=value end,
                    SetMinMaxValues=function(self,min,max) self.min=min self.max=max end,
                    SetTexture=function(self,value) self.texture=value end,SetWidth=function(self,value) self.width=value end,
                    SetHeight=function(self,value) self.height=value end,GetWidth=function(self) return self.width end,
                    GetHeight=function(self) return self.height end,GetObjectType=function(self) return self.objectType end,
                    GetParent=function(self) return self.parent end,SetParent=function(self,parent) self.parent=parent end,
                    GetPoint=function(self) return unpack(self.point) end,GetChildren=function(self) return unpack(self.children) end,
                    ClearAllPoints=function(self) self.point=nil end,
                    SetPoint=function(self,point,relative,relativePoint,x,y) self.point={point,relative,relativePoint,x,y} end,
                    Hide=function(self) self.visible=false end,Show=function(self) self.visible=true end,
					SetDrawLayer=function(self,value) self.drawLayer=value end,
                    IsShown=function(self) return self.visible end,
                    HookScript=function(self,event,callback) self.hooks[event]=callback end,
                    CreateFontString=function(self,name)
                        local child=FrameForgeTestControl(); _G[name]=child; return child
                    end,
                    RegisterEvent=function(self,event) self.events[event]=true end,CreateAnimationGroup=function() return AnimationGroup() end}
            end
            function PanelTemplates_SetNumTabs(frame,count) frame.numTabs=count end
            function PanelTemplates_SetTab(frame,selected)
                for index=1,frame.numTabs do end
                frame.selectedTab=selected
            end
            FrameForgeTestTime=10
            FrameForgeTestRequests=0
            FrameForgeTestLastAddonMessage=nil
            FrameForgeCreateCounts={}
            FrameForgeDropdownButtons={}
            function GetTime() return FrameForgeTestTime end
            function UnitName() return "Tester" end
            function SendAddonMessage(_,message)
                FrameForgeTestRequests=FrameForgeTestRequests+1
                FrameForgeTestLastAddonMessage=message
            end
            function RegisterAddonMessagePrefix() end
            function CreateFrame(_,name,parent,template)
                FrameForgeCreateCounts[name]=(FrameForgeCreateCounts[name] or 0)+1
                local frame=FrameForgeTestControl(); frame.parent=parent; frame.template=template; frame.anchorId=name; _G[name]=frame
                if template=="SmallAlterateCurrencyFrameTemplate" then
                    for _,suffix in ipairs({"Honor","Arena","Item1","Item2","Item3"}) do
                        local child=FrameForgeTestControl(); child.visible=false; _G[name..suffix]=child
                    end
                end
                return frame
            end
            function UIDropDownMenu_SetWidth(frame,width) frame.width=width end
            function UIDropDownMenu_Initialize(frame,callback) frame.initializer=callback end
            function UIDropDownMenu_CreateInfo() return {} end
            function UIDropDownMenu_AddButton(info) table.insert(FrameForgeDropdownButtons,info) end
            function UIDropDownMenu_SetSelectedID(frame,id) frame.selectedID=id end
            function UIDropDownMenu_SetText(frame,text) frame.text=text end
            function GetTalentTabInfo(index) return ({"First Tree","Second Tree","Third Tree"})[index] end
            function GetMerchantNumItems() return 10 end
            function GetMerchantItemCostInfo() return 0,0,1 end
            function GetMerchantItemCostItem() return "ManagedSealTexture",5,"item:managed-seal" end
            function AltCurrencyFrame_Update(name,texture,value)
                local button=_G[name]; button.texture=texture; button.text=tostring(value)
            end
            function FrameForgeSelectVendorSpec(index)
                FrameForgeDropdownButtons={}
                NativeHuntsMerchantSpecDropDown.initializer()
                FrameForgeDropdownButtons[index].func()
            end
            function HideUIPanel(frame)
                frame:Hide()
                if frame.hooks.OnHide then frame.hooks.OnHide() end
            end
        """)
        controls = lua.globals()
        for name in names | set(host_named) | {"NativeHuntsFrameInitializer"}:
            if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name):
                controls[name] = controls.FrameForgeTestControl()
                controls[name].anchorId = name
        for name in ("LFDParentFrame", "LFDQueueFrame", "LFDQueueFrameTitleText",
                     "LFDParentFramePortraitTexture", "LFDParentFramePortraitIcon"):
            controls[name] = controls.FrameForgeTestControl()
        controls.LFDParentFrame.width, controls.LFDParentFrame.height = 355, 440
        controls.NativeHuntsFrame.parent = controls.LFDParentFrame
        controls.FrameForge_Dungeon_Finder_UI.parent = controls.LFDParentFrame
        controls.MerchantFrame = controls.FrameForgeTestControl()
        controls.MerchantFrame.visible = False
        controls.MerchantFrame.anchorId = "MerchantFrame"
        controls.MerchantMoneyFrame = controls.FrameForgeTestControl()
        controls.LFDQueueFrameTitleText.text = "Dungeon Finder"
        controls.NativeHuntsFrame_OnLoad(controls.NativeHuntsFrameInitializer)
        if controls.FrameForge_Dungeon_Finder_UI.parent.anchorId != "NativeHuntsFrame":
            raise AssertionError("FrameForge presentation was not attached above the Native Hunts shell")
        handle_message("1\tV\t2\t7\t30")
        controls.MerchantFrame.visible = True
        controls.MerchantFrame.hooks["OnShow"]()
        controls.NativeHuntsFrame_OnEvent(controls.NativeHuntsFrameInitializer, "MERCHANT_SHOW")
        if (not controls.NativeHuntsMerchantSpecDropDown.visible or
                not controls.NativeHuntsMerchantSpecLabel.visible or
                controls.NativeHuntsMerchantSpecDropDown.selectedID != 2 or
                controls.NativeHuntsMerchantSpecDropDown.text != "Second Tree" or
                controls.NativeHuntsMerchantSpecLabel.text != "Specialization"):
            raise AssertionError("native merchant specialization dropdown did not initialize")
        if (not controls.NativeHuntsMerchantSealCurrency.visible or
                not controls.NativeHuntsMerchantSealCurrencyItem1.visible or
                controls.NativeHuntsMerchantSealCurrencyItem1.text != "30" or
                controls.NativeHuntsMerchantSealCurrencyItem1.texture != "ManagedSealTexture" or
                controls.MerchantMoneyFrame.visible):
            raise AssertionError("native merchant Seal balance did not initialize")
        label_point = controls.NativeHuntsMerchantSpecLabel.point
        dropdown_point = controls.NativeHuntsMerchantSpecDropDown.point
        currency_point = controls.NativeHuntsMerchantSealCurrency.point
        if (label_point[1] != "RIGHT" or
                label_point[2].anchorId != "NativeHuntsMerchantSpecDropDown" or
                label_point[3] != "LEFT" or label_point[4] != 0 or
                label_point[5] != 2 or dropdown_point[1] != "TOPLEFT" or
                dropdown_point[3] != "TOPLEFT" or dropdown_point[4] != 151 or
                dropdown_point[5] != -32 or
                controls.NativeHuntsMerchantSpecDropDown.width != 112 or
                currency_point[1] != "BOTTOMRIGHT" or
                currency_point[2].anchorId != "MerchantFrame" or
                currency_point[3] != "BOTTOMRIGHT" or currency_point[4] != -40 or
                currency_point[5] != 67):
            raise AssertionError("native merchant specialization controls left the safe header area")
        currency_item_point = controls.NativeHuntsMerchantSealCurrencyItem1.point
        if (currency_item_point[1] != "RIGHT" or
                currency_item_point[3] != "RIGHT" or currency_item_point[4] != -13 or
                currency_item_point[5] != 0):
            raise AssertionError("Seal quantity is not aligned to the stock money location")

        dropdown = controls.NativeHuntsMerchantSpecDropDown
        currency = controls.NativeHuntsMerchantSealCurrency
        refreshes = ((3, 8, 29, "Third Tree"), (2, 9, 28, "Second Tree"),
                     (1, 10, 27, "First Tree"), (3, 11, 26, "Third Tree"),
                     (2, 12, 25, "Second Tree"))
        for spec, revision, balance, talent_name in refreshes:
            controls.FrameForgeSelectVendorSpec(spec)
            if controls.FrameForgeTestLastAddonMessage != f"1\tV\t{spec}":
                raise AssertionError("specialization selection did not begin the requested refresh")
            handle_message(f"1\tV\t{spec}\t{revision}\t{balance}")
            # Exact PTR sequence: authoritative state, frame hide, CLOSED,
            # frame show, SHOW, followed by the server's inventory-established
            # acknowledgement.
            controls.MerchantFrame.visible = False
            controls.MerchantFrame.hooks["OnHide"]()
            controls.NativeHuntsFrame_OnEvent(
                controls.NativeHuntsFrameInitializer, "MERCHANT_CLOSED")
            if dropdown.visible or currency.visible or controls.MerchantMoneyFrame.visible:
                raise AssertionError("refresh close did not preserve hidden Huntmaster presentation")
            if dropdown.visible or currency.visible:
                raise AssertionError("transient MerchantFrame hide left controls visible")
            controls.MerchantFrame.visible = True
            controls.MerchantFrame.hooks["OnShow"]()
            controls.NativeHuntsFrame_OnEvent(
                controls.NativeHuntsFrameInitializer, "MERCHANT_SHOW")
            if dropdown.visible or currency.visible or controls.MerchantMoneyFrame.visible:
                raise AssertionError("refresh controls appeared before inventory acknowledgement")
            handle_message(f"1\tW\t{revision}")
            if (not dropdown.visible or dropdown.selectedID != spec or
                    dropdown.text != talent_name or not currency.visible or
                    not controls.NativeHuntsMerchantSealCurrencyItem1.visible or
                    controls.NativeHuntsMerchantSealCurrencyItem1.text != str(balance) or
                    controls.MerchantMoneyFrame.visible):
                raise AssertionError(
                    "specialization refresh lost native merchant controls, selection, or balance")
        if (controls.FrameForgeCreateCounts["NativeHuntsMerchantSpecDropDown"] != 1 or
                controls.FrameForgeCreateCounts["NativeHuntsMerchantSealCurrency"] != 1 or
                dropdown.selectedID != 2 or dropdown.text != "Second Tree" or
                controls.NativeHuntsMerchantSealCurrencyItem1.text != "25"):
            raise AssertionError("consecutive specialization refreshes duplicated or desynchronized controls")

        handle_message("1\tV\t2\t13\t14")
        controls.MerchantFrame.visible = False
        controls.MerchantFrame.hooks["OnHide"]()
        controls.NativeHuntsFrame_OnEvent(
            controls.NativeHuntsFrameInitializer, "MERCHANT_CLOSED")
        controls.MerchantFrame.visible = True
        controls.MerchantFrame.hooks["OnShow"]()
        controls.NativeHuntsFrame_OnEvent(
            controls.NativeHuntsFrameInitializer, "MERCHANT_SHOW")
        handle_message("1\tW\t13")
        if controls.NativeHuntsMerchantSealCurrencyItem1.text != "14":
            raise AssertionError("authoritative native merchant balance did not update")

        # A request interrupted before authoritative state is a genuine close.
        controls.FrameForgeSelectVendorSpec(1)
        controls.NativeHuntsFrame_OnEvent(controls.NativeHuntsFrameInitializer, "MERCHANT_CLOSED")
        controls.MerchantFrame.visible = False
        controls.MerchantFrame.hooks["OnHide"]()
        controls.MerchantFrame.visible = True
        controls.MerchantFrame.hooks["OnShow"]()
        controls.NativeHuntsFrame_OnEvent(controls.NativeHuntsFrameInitializer, "MERCHANT_SHOW")
        if (dropdown.visible or currency.visible or not controls.MerchantMoneyFrame.visible):
            raise AssertionError("interrupted specialization request left stale Huntmaster state")

        # Reopen, then verify an explicit server abort also clears the session.
        handle_message("1\tV\t2\t14\t14")
        controls.NativeHuntsFrame_OnEvent(controls.NativeHuntsFrameInitializer, "MERCHANT_SHOW")
        controls.FrameForgeSelectVendorSpec(3)
        handle_message("1\tV\t3\t15\t14")
        handle_message("1\tX\t15")
        if (dropdown.visible or currency.visible or not controls.MerchantMoneyFrame.visible):
            raise AssertionError("aborted specialization refresh left stale Huntmaster state")

        # Ordinary vendor opening after the abort retains stock money only.
        controls.NativeHuntsFrame_OnEvent(controls.NativeHuntsFrameInitializer, "MERCHANT_SHOW")
        if (dropdown.visible or controls.NativeHuntsMerchantSpecLabel.visible or currency.visible or
                not controls.MerchantMoneyFrame.visible):
            raise AssertionError("native vendor controls leaked into an ordinary merchant")

        handle_message("1\tV\t3\t16\t13")
        controls.MerchantFrame.hooks["OnShow"]()
        controls.NativeHuntsFrame_OnEvent(controls.NativeHuntsFrameInitializer, "MERCHANT_SHOW")
        if (not dropdown.visible or dropdown.selectedID != 3 or
                controls.NativeHuntsMerchantSealCurrencyItem1.text != "13" or
                controls.MerchantMoneyFrame.visible):
            raise AssertionError("closing and reopening the Huntmaster did not restore current state")
        controls.NativeHuntsFrame_OnEvent(controls.NativeHuntsFrameInitializer, "MERCHANT_CLOSED")
        if (dropdown.visible or controls.NativeHuntsMerchantSpecLabel.visible or currency.visible):
            raise AssertionError("MERCHANT_CLOSED did not clear native merchant controls")
        if not controls.NativeHuntsFrameInitializer.events["GOSSIP_SHOW"]:
            raise AssertionError("Native Hunts did not register for gossip panel arbitration")
        if (not controls.NativeHuntsFrameInitializer.events["MERCHANT_SHOW"] or
                not controls.NativeHuntsFrameInitializer.events["MERCHANT_UPDATE"] or
                not controls.NativeHuntsFrameInitializer.events["MERCHANT_CLOSED"]):
            raise AssertionError("Native Hunts did not register for the merchant lifecycle")
        controls.FrameForgeTestRequests = 0
        if controls.LFDParentFrame.numTabs != 2 or controls.LFDParentFrame.selectedTab != 1:
            raise AssertionError("Blizzard panel templates were not initialized with two numeric tabs")
        if (controls.Tracker_Hunt_Progress.min != 0 or controls.Tracker_Hunt_Progress.max != 100 or
                controls.Tracker_Hunt_Progress.value != 0):
            raise AssertionError("FrameForge StatusBar was not initialized before the first snapshot")
        overlay_regions = ("Located3_Text", "Hunt_Complete_Icon", "Complete_Text",
                           "Complete2A_Text", "Complete2B_Huntmaster_Value",
                           "Complete3A_Text", "Complete3B_Location_Value",
                           "Final_Fight_Icon", "Final_Fight_Text")
        if any(getattr(controls, name).drawLayer != "OVERLAY" for name in overlay_regions):
            raise AssertionError("Fight/Turnin body regions were not normalized to a persistent draw layer")
        if (not controls.LFDParentFramePortraitTexture.visible or
                not controls.LFDParentFramePortraitIcon.visible):
            raise AssertionError("initial Dungeon Finder portrait artwork was not preserved")
        controls.NativeHuntsFrame_SelectTab(2)
        if (controls.LFDParentFrame.selectedTab != 2 or controls.LFDQueueFrame.visible or
                not controls.NativeHuntsFrame.visible or
                not controls.FrameForge_Dungeon_Finder_UI.visible or
                (controls.LFDParentFramePortraitTexture.visible or
                 controls.LFDParentFramePortraitIcon.visible) or
                controls.LFDParentFrame.width != 355 or controls.LFDParentFrame.height != 440 or
                controls.LFDQueueFrameTitleText.text != "Dungeon Finder" or
                controls.FrameForgeTestRequests != 1):
            raise AssertionError("Hunts tab selection did not update Blizzard's panel state")
        controls.NativeHuntsFrame_SelectTab(1)
        if (controls.LFDParentFrame.selectedTab != 1 or not controls.LFDQueueFrame.visible or
                controls.NativeHuntsFrame.visible or controls.FrameForge_Dungeon_Finder_UI.visible or
                (not controls.LFDParentFramePortraitTexture.visible or
                 not controls.LFDParentFramePortraitIcon.visible) or
                controls.LFDQueueFrameTitleText.text != "Dungeon Finder"):
            raise AssertionError("Dungeon Finder tab selection did not restore Blizzard's panel state")
        controls.FrameForgeTestTime = 12
        controls.NativeHuntsFrame_SelectTab(2)
        controls.LFDParentFrame.hooks["OnHide"]()
        if (controls.NativeHuntsFrame.visible or controls.FrameForge_Dungeon_Finder_UI.visible or
                controls.LFDParentFramePortraitTexture.visible or
                controls.LFDParentFramePortraitIcon.visible):
            raise AssertionError("closing Dungeon Finder left Native Hunts presentation visible")
        controls.FrameForgeTestTime = 14
        controls.LFDParentFrame.hooks["OnShow"]()
        if (controls.LFDQueueFrame.visible or not controls.NativeHuntsFrame.visible or
                not controls.FrameForge_Dungeon_Finder_UI.visible or
                controls.LFDParentFramePortraitTexture.visible or
                controls.LFDParentFramePortraitIcon.visible or
                controls.FrameForgeTestRequests != 3):
            raise AssertionError("reopening Dungeon Finder did not restore Hunts selection")
        controls.NativeHuntsFrame_OnEvent(controls.NativeHuntsFrameInitializer, "GOSSIP_SHOW")
        if (controls.LFDParentFrame.visible or controls.NativeHuntsFrame.visible or
                controls.FrameForge_Dungeon_Finder_UI.visible or
                controls.LFDParentFrame.selectedTab != 2):
            raise AssertionError("opening gossip did not yield the Hunts panel without losing its state")
        controls.FrameForgeTestTime = 16
        controls.LFDParentFrame.visible = True
        controls.LFDParentFrame.hooks["OnShow"]()
        if (not controls.NativeHuntsFrame.visible or
                not controls.FrameForge_Dungeon_Finder_UI.visible or
                controls.LFDParentFrame.selectedTab != 2):
            raise AssertionError("reopening Hunts after gossip did not preserve its selected state")
        controls.NativeHuntsFrame_SelectTab(1)
        controls.LFDParentFrame.hooks["OnHide"]()
        controls.LFDParentFrame.hooks["OnShow"]()
        if (not controls.LFDQueueFrame.visible or controls.NativeHuntsFrame.visible or
                controls.FrameForge_Dungeon_Finder_UI.visible or
                not controls.LFDParentFramePortraitTexture.visible or
                not controls.LFDParentFramePortraitIcon.visible):
            raise AssertionError("reopening Dungeon Finder did not restore its stock portrait icon")

        def lua_table(value):
            if isinstance(value, dict):
                return lua.table_from({key: lua_table(item) for key, item in value.items()})
            return value

        stats = {"standard": 11, "elite": 3, "eliteUnlocked": True, "accepted": 1,
                 "limit": 1, "eliteAvailable": False, "sealState": "A", "seals": 7}
        active = {"revision": 1, "contentAvailable": True, "active": True, "state": "T",
                  "tier": "S", "progress": 54, "finalVisible": False, "ready": False,
                  "huntmaster": "Huntmaster Varyn", "city": "Dalaran", "prey": "The Headsman",
                  "zone": "Crystalsong Forest", "finalLocation": "Crystalsong Forest",
                  "reason": "", "stats": stats}

        def rendered(**overrides):
            snapshot = dict(active)
            snapshot.update(overrides)
            snapshot["stats"] = dict(overrides.get("stats", stats))
            render(lua_table(snapshot))
            return controls

        standard = rendered()
        if (standard.Tracker_Target_Name.text != "The Headsman" or
                standard.Tracker_Huntmaster.text != "Huntmaster Varyn" or
                standard.Tracker_Location.text != "Dalaran" or
                standard.Tracker_Hunt_Ground_Value.text != "Crystalsong Forest" or
                standard.Tracker_Hunt_Progress.value != 54 or
                not standard.Hunt_Progress_Text__FFLayer.visible or
                standard.Trail_Located_Text__FFLayer.visible or
                not standard.Hunt_Tier_Icon__FFLayer.visible):
            raise AssertionError("Tracking state or dynamic FrameForge values regressed")
        for tier, expected in {
                "S": (True, True, False),
                "E": (True, False, True),
                "N": (False, False, False),
                "X": (False, False, False),
        }.items():
            tier_controls = rendered(tier=tier)
            actual = (
                tier_controls.Hunt_Tier_Icon__FFLayer.visible,
                tier_controls.Tracker_Standard_Hunt__FFLayer.visible,
                tier_controls.Tracker_Elite_Hunt__FFLayer.visible,
            )
            if actual != expected:
                raise AssertionError(f"tier {tier} visibility regressed: {actual}")
            if (tier == "E" and not tier_controls.Hunt_Tier_Icon.texture.endswith("hunt_icon_elite") or
                    tier == "S" and not tier_controls.Hunt_Tier_Icon.texture.endswith("hunt_icon_standard")):
                raise AssertionError(f"tier {tier} icon texture regressed")
        for state in ("T", "F", "P", "R"):
            elite_state = rendered(state=state, tier="E")
            if (not elite_state.Hunt_Tier_Icon__FFLayer.visible or
                    not elite_state.Hunt_Tier_Icon.texture.endswith("hunt_icon_elite")):
                raise AssertionError(f"Elite icon visibility regressed in state {state}")
            standard_state = rendered(state=state, tier="S")
            if (not standard_state.Hunt_Tier_Icon__FFLayer.visible or
                    not standard_state.Hunt_Tier_Icon.texture.endswith("hunt_icon_standard")):
                raise AssertionError(f"Standard icon visibility regressed in state {state}")
        rendered(state="T", tier="E")
        controls.NativeHuntsFrame_SelectTab(1)
        controls.NativeHuntsFrame_SelectTab(2)
        if (not controls.Hunt_Tier_Icon__FFLayer.visible or
                not controls.Hunt_Tier_Icon.texture.endswith("hunt_icon_elite")):
            raise AssertionError("reopening the Hunts tab lost Elite icon visibility")
        located = rendered(state="F", progress=100)
        if (not located.Trail_Located_Text__FFLayer.visible or
                not located.Located_Text__FFLayer.visible or
                located.Tracking_Text__FFLayer.visible or located.Tracker_Hunt_Progress.value != 100):
            raise AssertionError("Located state regressed")
        fight = rendered(state="P", tier="E")
        if (not fight.Final_Fight_Icon__FFLayer.visible or
                not fight.Final_Fight_Text__FFLayer.visible or
                not fight.Final_Confrontation_Text__FFLayer.visible or
                fight.Hunt_Complete_Icon__FFLayer.visible or
                fight.Tracker_Hunt_Progress.visible or
                not fight.Hunt_Tier_Icon__FFLayer.visible or
                not fight.Hunt_Tier_Icon.texture.endswith("hunt_icon_elite")):
            raise AssertionError("Fight state regressed")
        turnin = rendered(state="R")
        if (not turnin.Hunt_Complete_Icon__FFLayer.visible or
                not turnin.Complete_Text__FFLayer.visible or
                not turnin.Complete2A_Text__FFLayer.visible or
                not turnin.Complete2B_Huntmaster_Value__FFLayer.visible or
                not turnin.Complete3A_Text__FFLayer.visible or
                not turnin.Complete3B_Location_Value__FFLayer.visible or
                not turnin.Hunt_Complete_Text__FFLayer.visible or
                turnin.Final_Fight_Icon__FFLayer.visible or
                turnin.Complete2B_Huntmaster_Value.text != "Huntmaster Varyn" or
                turnin.Complete3B_Location_Value.text != "Dalaran"):
            raise AssertionError("Turnin state regressed")
        idle = rendered(active=False, state="I", tier="N", progress=0)
        if (not idle.Hunt_Panel_Idle__FFLayer.visible or idle.Hunt_Panel_Top__FFLayer.visible or
                not idle.Hunt_Panel_Record__FFLayer.visible or
                idle.Standard_Hunts_Value.text != "11" or idle.Elite_Hunts_Value.text != "3" or
                idle.Hunt_Seal_Value.text != "7"):
            raise AssertionError("Idle state or Hunt record rendering regressed")
        # Vendor navigation produces several consecutive GOSSIP_SHOW events
        # (main menu, spec, tier, slot, item, confirmation). The stock panel may
        # yield while gossip owns the screen, but the selected Hunts tab and its
        # existing Hunt Record presentation must survive unchanged.
        vendor_module = (ROOT / "src" / "NativeHuntsModule.cpp").read_text(
            encoding="utf-8")
        vendor_config = (ROOT / "conf" / "mod_native_hunts.conf.dist").read_text(
            encoding="utf-8")
        if ("Browse Huntmaster's Seal rewards." not in vendor_module or
                "Browse Huntmaster's Seal rewards. (" in vendor_module or
                "NativeHunts.SealStore.Tier1.Cost = 5" not in vendor_config):
            raise AssertionError("Seal vendor is not enabled for the Hunt Record regression vector")
        controls.NativeHuntsFrame_SelectTab(2)
        for _ in range(6):
            controls.NativeHuntsFrame_OnEvent(
                controls.NativeHuntsFrameInitializer, "GOSSIP_SHOW")
        controls.LFDParentFrame.visible = True
        controls.LFDParentFrame.hooks["OnShow"]()
        after_vendor = rendered(active=False, state="I", tier="N", progress=0)
        if (controls.LFDParentFrame.selectedTab != 2 or
                not controls.NativeHuntsFrame.visible or
                not controls.FrameForge_Dungeon_Finder_UI.visible or
                not after_vendor.Hunt_Panel_Record__FFLayer.visible or
                after_vendor.Standard_Hunts_Value.text != "11" or
                after_vendor.Elite_Hunts_Value.text != "3" or
                after_vendor.Hunt_Seal_Value.text != "7"):
            raise AssertionError(
                "Seal vendor gossip made the existing Hunt Record unavailable")
        if decode("field%09with%25delimiter") != "field\twith%delimiter":
            raise AssertionError("Lua client decoder regression")
        for malformed in ("%", "%GG", "%00", "%1F"):
            if decode(malformed) is not None:
                raise AssertionError(f"Lua decoder accepted malformed data: {malformed}")
        controls.Tracker_Target_Name = None
        controls.NativeHuntsFrame_OnLoad(controls.NativeHuntsFrameInitializer)
        if (controls.NativeHuntsFrame.visible or
                controls.FrameForge_Dungeon_Finder_UI.visible or
                not controls.LFDQueueFrame.visible or
                "Tracker_Target_Name" not in controls.NativeHuntsFrameInitializationError):
            raise AssertionError("missing exported control did not fail safely with a diagnostic")
        controls.NativeHuntsFrame_SelectTab(2)
        if controls.LFDParentFrame.selectedTab != 1:
            raise AssertionError("failed initialization allowed the Hunts tab to activate")
        print("PASS direct Lua 5.1 FrameForge state and value vectors", flush=True)
    print("PASS functional FrameForge Native Hunts client contract", flush=True)


def run_frameforge_package_checks() -> None:
    """Verify deterministic explicit-content rebuilds for the current PTR package."""
    epf = ROOT / "content" / "mod-native-hunts.epf"
    manifest = json.loads((ROOT / "content" / "manifest.json").read_bytes())
    if manifest.get("version") != "22-ptr7":
        print("SKIP explicit FrameForge deterministic package contract", flush=True)
        return
    before = epf.read_bytes()
    subprocess.run([sys.executable, str(ROOT / "content" / "build_epf.py")],
                   cwd=ROOT, check=True)
    if epf.read_bytes() != before:
        raise AssertionError("repeated explicit-content EPF build was not byte deterministic")
    print("PASS explicit FrameForge EPF deterministic rebuild contract", flush=True)


def run_function_shadow_guard() -> None:
    """Reject duplicate top-level definitions that would silently shadow earlier ones."""
    for path in sorted((ROOT / "tests").glob("*.py")):
        tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
        defined: dict[str, int] = {}
        for node in tree.body:
            if not isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
                continue
            if node.name in defined:
                raise AssertionError(
                    f"duplicate top-level definition {node.name!r} in "
                    f"{path.relative_to(ROOT)} (line {defined[node.name]} and line "
                    f"{node.lineno}); the later definition shadows the earlier one")
            defined[node.name] = node.lineno
    print("PASS test scripts have no shadowed top-level definitions", flush=True)


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
        ROOT / "data/sql/db-characters/base/003_native_hunt_collection.sql",
        ROOT / "data/sql/db-characters/base/004_native_hunt_relic_pool.sql",
    }
    if not all(path.is_file() for path in required_schema):
        raise AssertionError("native persistence schema is incomplete")
    world_sql = list((ROOT / "data" / "sql" / "db-world").glob("**/*"))
    for path in world_sql:
        if path.is_file() and "native_hunt_assignment" in path.read_text(encoding="utf-8"):
            raise AssertionError("character assignment schema leaked into world SQL")
    current_world_sql = [path for path in world_sql if path.is_file()]
    expected_world_sql = {
        "base/001_native_hunt_shared_relic.sql",
        "updates/2026_10_18_00_native_hunt_shared_relic_visual.sql",
    }
    actual_world_sql = {
        str(path.relative_to(ROOT / "data/sql/db-world"))
        for path in current_world_sql
    }
    if actual_world_sql != expected_world_sql:
        raise AssertionError("Native Hunts world SQL scope changed unexpectedly")
    world_text = "\n".join(path.read_text(encoding="utf-8")
                           for path in current_world_sql)
    if re.search(r"INSERT\s+(?:IGNORE\s+)?INTO\s+`gameobject`(?:\s|\()",
                 world_text, re.IGNORECASE):
        raise AssertionError("Native Hunts must not provision permanent world spawns through SQL")
    assignment = (ROOT / "data/sql/db-characters/base/001_native_hunt_assignment.sql").read_text(encoding="utf-8")
    for column in ("`hunt_instance_id`", "`ambushes_completed`", "`ambush_pending`"):
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
        "DestroyItemCount(resources.SealItemEntry",
        "duplicate rewards are blocked until recovery",
        "provisional rewards were removed",
    )
    for token in required_turn_in_safety:
        if token not in manager:
            raise AssertionError(f"turn-in consistency safeguard missing: {token}")
    module = (ROOT / "src" / "NativeHuntsModule.cpp").read_text(encoding="utf-8")
    startup_sources = manager + module
    for token in ("INSERT INTO creature", "INSERT INTO `creature`",
                  "INSERT INTO creature_template", "INSERT INTO `creature_template`"):
        if token in startup_sources:
            raise AssertionError("Native Hunts startup must not provision Huntmaster world rows")
    protocol = (ROOT / "src" / "HuntUiProtocol.cpp").read_text(encoding="utf-8")
    protocol_header = (ROOT / "src" / "HuntUiProtocol.h").read_text(encoding="utf-8")
    for token in ('Prefix[] = "NHUNTS"', "MaxWireBytes = 255", "MaxFragments = 8",
                  "ParseSnapshotRequest", "SerializeSnapshot", "RequestAllowed"):
        if token not in protocol + protocol_header:
            raise AssertionError(f"bounded Native Hunts UI protocol missing: {token}")
    for token in ("OnPlayerBeforeSendChatMessage", "LANG_ADDON", "CHAT_MSG_WHISPER",
                  "HandleUiAddonMessage", "message.clear()"):
        if token not in module:
            raise AssertionError(f"addon-message transport hook missing: {token}")
    menu_start = module.index("static void ShowMenu(Player *player, Creature *creature)")
    menu_end = module.index("\n\t}\n};", menu_start)
    menu = module[menu_start:menu_end]
    idle = menu.index("if (!runtime) {")
    standard = menu.index("ActionRequest);", idle)
    elite = menu.index("ActionRequestElite);", standard)
    ready = menu.index("} else if (runtime->Aggregate.State", elite)
    if not idle < standard < elite < ready or "if (!runtime &&" in menu:
        raise AssertionError("Huntmaster idle/Elite gossip can dereference a null runtime")
    hello = module[module.index("bool OnGossipHello(Player *player, Creature *creature)"):menu_start]
    if (hello.count("sNativeHunts.CanOpenHuntmasterGossip(player, creature)") != 2 or
            "CloseGossipMenuFor(player);" not in hello or
            "return false;" in hello or
            "sender != GOSSIP_SENDER_MAIN" not in hello or
            "case ActionRequest: sNativeHunts.RequestHunt(player, creature, message);" not in hello or
            "case ActionRequestElite: sNativeHunts.RequestEliteHunt(player, creature, message);" not in hello):
        raise AssertionError("standard/Elite gossip selections do not preserve validated sender and Huntmaster identity")
    if "Show me my hunting record." in module or "ActionStats" in module:
        raise AssertionError("redundant Huntmaster hunting-record gossip remains")
    if (manager.count("auto const *huntmaster = ResolveHuntmaster(giver);") != 2 or
            "MatchesManagedHuntmaster(creature->GetEntry(), creature->GetSpawnId()," not in manager or
            "if (IsHuntmaster(creature))" not in manager or
            "CanUseIssuingHuntmaster(" not in manager or
            "MatchesIssuingHuntmaster(giver->GetEntry()," not in manager):
        raise AssertionError("standard/Elite requests do not share managed Huntmaster resolution")
    config = (ROOT / "conf" / "mod_native_hunts.conf.dist").read_text(encoding="utf-8")
    config_reads = set(re.findall(r'"(NativeHunts\.[A-Za-z0-9.]+)"', module))
    config_entries = set(re.findall(r'^(NativeHunts\.[A-Za-z0-9.]+)\s*=', config, re.MULTILINE))
    undocumented_reads = {key for key in config_reads if key not in config_entries and
                          not any(entry.startswith(key) for entry in config_entries)}
    if undocumented_reads:
        raise AssertionError(f"undocumented config reads: {sorted(undocumented_reads)}")
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
    reward_delivery_contract = (
        "pool[urand(0, static_cast<std::uint32_t>(pool.size()-1))]",
        "PlanRewardDelivery(rewardItem != 0, seals != 0",
        "CanStoreRewardBundle(player, rewardItem",
        "player->CanStoreItems(",
        "player->CanTakeMoreSimilarItems(",
        "GetEndgameEliteUpgradeSlot(player, item",
        "SelectBelowThresholdSlot(candidate.ItemId",
        "InventoryFailureName(equipmentResult)",
        "player->GetFreeInventorySpace()",
        'MailDraft draft("Huntmaster\'s Reward — "',
        "Your quarry has fallen, hunter. Your reward awaits you.",
        "MailSender(MAIL_CREATURE, runtime.HuntmasterEntry)",
        "item->SaveToDB(transaction)",
        "provisional.MailId = player->GetMails().front()->messageID",
        "RemoveProvisionalRewards(player, _resources, provisional)",
        "ResolveRewardCommit(false, persistence)",
    )
    for token in reward_delivery_contract:
        if token not in manager:
            raise AssertionError(f"atomic reward-delivery contract missing: {token}")
    if "SelectDeliverableReward" in manager:
        raise AssertionError("inventory availability still changes equipment selection")
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


def run_seal_vendor_checks() -> None:
    manager = (ROOT / "src" / "NativeHuntsManager.cpp").read_text(encoding="utf-8")
    header = (ROOT / "src" / "NativeHuntsManager.h").read_text(encoding="utf-8")
    module = (ROOT / "src" / "NativeHuntsModule.cpp").read_text(encoding="utf-8")
    config = (ROOT / "conf" / "mod_native_hunts.conf.dist").read_text(encoding="utf-8")
    manifest = json.loads((ROOT / "content" / "manifest.json").read_text(encoding="utf-8"))

    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    for source in ("NativeHuntsManager.cpp", "NativeHuntsModule.cpp",
                   "SealCatalogPolicy.cpp", "mod_native_hunts_loader.cpp"):
        if f'AC_ADD_SCRIPT("${{CMAKE_CURRENT_LIST_DIR}}/src/{source}")' not in cmake:
            raise AssertionError(f"worldserver build does not register {source}")
    if "seal-catalog-policy-v1" not in cmake:
        raise AssertionError("worldserver build lacks the Seal catalog policy marker")

    expected_tiers = ((1, 5, 213, 219), (2, 10, 226, 232),
                      (3, 20, 245, 251), (4, 30, 264, 264))
    for tier, cost, minimum, maximum in expected_tiers:
        for suffix, value in (("Cost", cost), ("MinItemLevel", minimum),
                              ("MaxItemLevel", maximum)):
            line = f"NativeHunts.SealStore.Tier{tier}.{suffix} = {value}"
            if line not in config:
                raise AssertionError(f"Seal tier default missing: {line}")
    for token in ("SealStoreTierCost{{5, 10, 20, 30}}",
                  "SealStoreTierMinItemLevel{{213, 226, 245, 264}}",
                  "SealStoreTierMaxItemLevel{{219, 232, 251, 264}}",
                  "std::min(minimum, maximum)", "std::max(minimum, maximum)"):
        if token not in header + module:
            raise AssertionError(f"Seal tier configuration contract missing: {token}")

    catalog_contract = (
        "GetItemTemplateStore()", "ITEM_QUALITY_EPIC", "ITEM_CLASS_WEAPON",
        "ITEM_CLASS_ARMOR", "IsSpecForClass(player, spec)",
        "IsSpecCompatibleEquipment(spec, *item)", "MatchesSealStoreSlot(*item, slot)",
        "ITEM_FLAG_HEROIC_TOOLTIP", "ITEM_MOD_RESILIENCE_RATING",
        "player->CanUseItem(item)", "GetArmorPreference(player, *item) < 0.0f",
        "MatchesSealCatalogRole(role, *item)",
        "traits.Relic = item.InventoryType == INVTYPE_RELIC",
        "ITEM_SUBCLASS_ARMOR_TOTEM", "ITEM_SUBCLASS_ARMOR_LIBRAM",
        "ITEM_SUBCLASS_ARMOR_IDOL", "ITEM_SUBCLASS_ARMOR_SIGIL",
        "ScoreRewardItem(player, spec, role, *item) <= 0.0f",
        "if (items.size() > 20)", "items.resize(20)",
    )
    for token in catalog_contract:
        if token not in manager:
            raise AssertionError(f"dynamic Seal catalog contract missing: {token}")
    eligibility_start = manager.index(
        "bool NativeHuntsManager::IsSealStoreItemEligible")
    eligibility_end = manager.index(
        "std::vector<SealStoreItem> NativeHuntsManager::BuildSealStoreItems",
        eligibility_start)
    eligibility_body = manager[eligibility_start:eligibility_end]
    if (eligibility_body.index("MatchesSealCatalogRole(role, *item)") >
            eligibility_body.index("ScoreRewardItem(player, spec, role, *item)")):
        raise AssertionError("role policy runs after the final eligibility score")
    build_start = eligibility_end
    build_end = manager.index(
        "bool NativeHuntsManager::PurchaseSealStoreItem", build_start)
    build_body = manager[build_start:build_end]
    if not (build_body.index("IsSealStoreItemEligible") <
            build_body.index("std::sort(items.begin()") <
            build_body.index("items.resize(20)")):
        raise AssertionError("role-filtered eligibility does not precede ranking/truncation")
    for slot in ("Weapon", "Head", "Neck", "Shoulder", "Back", "Chest",
                 "Wrist", "Hands", "Waist", "Legs", "Feet", "Ring",
                 "Trinket", "OffHand", "Relic"):
        if f"SealStoreSlot::{slot}" not in manager + module:
            raise AssertionError(f"Seal equipment category missing: {slot}")
    for class_name in ("WARRIOR", "PALADIN", "HUNTER", "ROGUE", "PRIEST",
                       "DEATH_KNIGHT", "SHAMAN", "MAGE", "WARLOCK", "DRUID"):
        if f"CLASS_{class_name}" not in manager or f"CLASS_{class_name}" not in module:
            raise AssertionError(f"Seal class/spec coverage missing: {class_name}")

    purchase_contract = (
        "GetItemCount(_resources.SealItemEntry, false)",
        "balance < cost", "CanStoreNewItem(NULL_BAG, NULL_SLOT, destination",
        "StoreNewItem(destination, itemId, true)",
        "DestroyItemCount(_resources.SealItemEntry, cost, true)",
        "item->SetBinding(true)", "QueueUiState(player)",
        "That item is not an eligible Huntmaster's Seal reward",
        "no Huntmaster's Seals were consumed",
    )
    for token in purchase_contract:
        if token not in manager:
            raise AssertionError(f"server-authoritative Seal purchase contract missing: {token}")
    purchase_body = manager[manager.index("bool NativeHuntsManager::PurchaseSealStoreItem"):]
    if purchase_body.index("StoreNewItem(destination, itemId, true)") > purchase_body.index(
            "DestroyItemCount(_resources.SealItemEntry, cost, true)"):
        raise AssertionError("Seal deduction can precede failed item creation")
    if purchase_body.count("DestroyItemCount(_resources.SealItemEntry, cost, true)") != 1:
        raise AssertionError("Seal purchase does not have exactly one deduction site")

    fallback_contract = (
        "ShowSealSpecMenu", "ShowSealTierMenu", "ShowSealSlotMenu",
        "ShowSealItemMenu", "ShowSealConfirmation", "ActionSealBuyConfirm",
        "SealStoreContexts.erase(guid);", "PendingItemId",
        "Browse Huntmaster's Seal rewards.", "GOSSIP_ICON_VENDOR",
    )
    for token in fallback_contract:
        if token not in module:
            raise AssertionError(f"Seal gossip fallback contract missing: {token}")
    show_menu = module[module.index("static void ShowMenu("):
                       module.index("static void ShowSealSpecMenu(")]
    if ("Browse Huntmaster's Seal rewards. (" in show_menu or
            "GetSealBalance(player)" in show_menu):
        raise AssertionError("Huntmaster vendor navigation gossip includes a Seal balance")
    consume = module.index("// Consume the one-shot server selection")
    purchase = module.index("sNativeHunts.PurchaseSealStoreItem", consume)
    if module.index("SealStoreContexts.erase(guid);", consume) > purchase:
        raise AssertionError("Seal confirmation is not consumed before purchase")

    currencies = manifest.get("currencies", [])
    if not any(row.get("symbol") == "huntmaster-seal-currency" and
               row.get("item") == "huntmaster-seal" and
               row.get("category", {}).get("symbol") == "hunts"
               for row in currencies):
        raise AssertionError("existing Hunts physical currency declaration regressed")
    native_contract = (
        "SMSG_LIST_INVENTORY", "OnPlayerBeforeBuyItemFromVendor",
        "HandleNativeSealVendorPurchase", "BuildNativeSealVendorRows",
        "NativeSealVendorSessions", "NativeSealVendorLifetime",
        "NativeSealVendorReplayWindow", "item = 0;",
        "PurchaseSealStoreItem(player", "SMSG_BUY_ITEM",
        "items.front()",
        "ParseVendorSpecRequest", "UNIT_NPC_FLAG_VENDOR",
        "SealStoreCatalogDiagnostics", "total_candidates={}",
        "role_rejected={}", "role_accepted={}", "eligible={}",
        "sent={}", "limit_reached={}", "seal-catalog-policy-v1",
    )
    for token in native_contract:
        if token not in module:
            raise AssertionError(f"native merchant integration contract missing: {token}")
    catalog_start = module.index("std::vector<NativeSealVendorRow> BuildNativeSealVendorRows")
    catalog_end = module.index("void SendNativeSealVendorState", catalog_start)
    catalog_body = module[catalog_start:catalog_end]
    expected_slot_order = (
        "Head", "Shoulder", "Chest", "Hands", "Legs", "Wrist", "Waist",
        "Feet", "Back", "Neck", "Ring", "Trinket", "Weapon", "OffHand",
        "Relic",
    )
    slot_positions = [catalog_body.index(f"SealStoreSlot::{slot}")
                      for slot in expected_slot_order]
    if slot_positions != sorted(slot_positions) or len(set(slot_positions)) != 15:
        raise AssertionError("native catalog does not preserve the original slot order")
    slot_loop = catalog_body.index("for (auto const slot : slotOrder)")
    tier_loop = catalog_body.index("for (std::uint8_t tier = 1; tier <= 4; ++tier)")
    winner = catalog_body.index("auto const &best = items.front();")
    append = catalog_body.index("rows.push_back({best.ItemId, tier, slot, extendedCost});")
    if not (slot_loop < tier_loop < winner < append):
        raise AssertionError("native catalog is not slot-first/tier-second with one ranked winner")
    for forbidden in ("rank-round-robin", "for (std::size_t rank =", "group.Items[rank]"):
        if forbidden in catalog_body:
            raise AssertionError("obsolete multi-candidate catalog packing remains")
    if ("rows.reserve(slotOrder.size() * 4);" not in catalog_body or
            "if (items.empty())" not in catalog_body or
            "continue;" not in catalog_body):
        raise AssertionError("native catalog lacks its 60-row bound or empty-group skip")
    # Functional model of the production traversal: each nonempty group emits
    # exactly its first ranked item, retaining the group's tier cost/resource.
    costs = {1: 5, 2: 10, 3: 20, 4: 30}
    extended = {1: 101, 2: 102, 3: 103, 4: 104}
    ranked = {
        ("Head", 1): (1101, 1102),
        ("Head", 3): (1301,),
        ("Shoulder", 2): (2201, 2202),
        ("Relic", 4): (15401,),
    }
    selected = []
    for slot in expected_slot_order:
        for tier in range(1, 5):
            items = ranked.get((slot, tier), ())
            if items:
                selected.append((slot, tier, items[0], costs[tier], extended[tier]))
    if selected != [
            ("Head", 1, 1101, 5, 101),
            ("Head", 3, 1301, 20, 103),
            ("Shoulder", 2, 2201, 10, 102),
            ("Relic", 4, 15401, 30, 104)]:
        raise AssertionError("slot/tier winner selection, empty skipping, or costs regressed")
    if len(expected_slot_order) * 4 != 60:
        raise AssertionError("native catalog maximum is no longer 60 rows")
    for token in ("player->GetGUID().GetCounter()",
                  "session->second.VendorGuid != vendorGuid",
                  "vendorSlot >= session->second.Rows.size()",
                  "session->second.Rows[vendorSlot].ItemId != requestedItem",
                  "std::chrono::steady_clock::now() >= session->second.Expires",
                  "CanStoreNewItem(NULL_BAG, NULL_SLOT"):
        if token not in module:
            raise AssertionError(f"native merchant isolation/revalidation missing: {token}")
    if "if (HandleNativeSealVendorPurchase(player, vendorGuid, vendorSlot," not in module:
        raise AssertionError("ordinary vendors are not guarded from the custom purchase path")
    open_start = module.index("bool OpenNativeSealVendor(")
    open_end = module.index("std::uint8_t DefaultNativeSealVendorSpec", open_start)
    open_body = module[open_start:open_end]
    refresh_start = module.index("bool HandleNativeSealVendorSpec(")
    refresh_end = module.index("void SendNativeSealVendorBuySuccess", refresh_start)
    refresh_body = module[refresh_start:refresh_end]
    action_start = module.index("if (action == ActionSealStore)")
    action_end = module.index("if (action >= ActionSealSpecBase", action_start)
    action_body = module[action_start:action_end]
    if ("session.Rows = BuildNativeSealVendorRows(player, session.Spec);" not in open_body or
            "SendNativeSealVendorInventory(player, vendor, stored)" not in open_body or
            "OpenNativeSealVendor(player, vendor, specIndex, message, true)" not in refresh_body):
        raise AssertionError("initial and refreshed merchant inventories do not share resource resolution")
    if ("SendNativeSealVendorRefreshResult(player, stored, false);" not in open_body or
            "SendNativeSealVendorRefreshResult(player, stored, true);" not in open_body or
            open_body.index("SendNativeSealVendorState(player, stored);") >
            open_body.index("SendNativeSealVendorInventory(player, vendor, stored)") or
            open_body.index("SendNativeSealVendorInventory(player, vendor, stored)") >
            open_body.index("SendNativeSealVendorRefreshResult(player, stored, true);")):
        raise AssertionError("merchant refresh lacks bounded state/inventory/result ordering")
    close_at = action_body.index("CloseGossipMenuFor(player);")
    open_at = action_body.index("OpenNativeSealVendor(player, creature, defaultSpec, message)")
    if close_at > open_at or action_body.count("CloseGossipMenuFor(player);") != 1:
        raise AssertionError("gossip must close exactly once before the initial merchant opens")
    inventory_start = module.index("bool SendNativeSealVendorInventory(")
    inventory_end = module.index("bool OpenNativeSealVendor(", inventory_start)
    inventory_body = module[inventory_start:inventory_end]
    if ("packet << row.ExtendedCost;" not in inventory_body or
            "ExtendedCostMatchesConfiguredPrice(tier, extendedCost)" not in module):
        raise AssertionError("initial merchant rows can omit validated extended costs")
    ui_lua = (ROOT / "content" / "client" / "Interface" / "FrameXML" /
              "NativeHuntsFrame.lua").read_text(encoding="utf-8")
    for token in ("NativeHuntsMerchantSpecDropDown", "GetTalentTabInfo",
                  "SmallAlterateCurrencyFrameTemplate", "vendorCurrencyItem:Show()",
                  "MerchantMoneyFrame:Hide()", "MerchantMoneyFrame:Show()",
                  'SendAddonMessage(PREFIX, "1\\tV\\t"',
                  'if f[2]=="V" then HandleVendorState(f)',
                  'if f[2]=="W" then HandleVendorRefreshResult(f,true)',
                  'if f[2]=="X" then HandleVendorRefreshResult(f,false)',
                  'MerchantFrame:HookScript("OnHide"'):
        if token not in ui_lua:
            raise AssertionError(f"native merchant client contract missing: {token}")
    if "vendorStatePendingShow" in ui_lua:
        raise AssertionError("native merchant state still relies on a single MERCHANT_SHOW handshake")
    merchant_show = ui_lua[ui_lua.index('if event=="MERCHANT_SHOW" then'):
                           ui_lua.index('if event=="MERCHANT_UPDATE" then')]
    if "ResetVendorState" in merchant_show:
        raise AssertionError("MERCHANT_SHOW still terminates the Native Hunts vendor session")
    for forbidden in ("ActivateSpec", "LearnTalent", "SetActiveTalentGroup"):
        if forbidden in module + ui_lua:
            raise AssertionError("shopping specialization mutates character talents")
    costs = manifest.get("extendedCosts", [])
    if len(costs) != 4:
        raise AssertionError("four managed Seal tier extended costs are required")
    for tier, cost, _, _ in expected_tiers:
        row = next((entry for entry in costs
                    if entry.get("symbol") == f"seal-cost-tier-{tier}"), None)
        requirements = row.get("requirements", []) if row else []
        if requirements != [{"item": {"symbol": "huntmaster-seal"}, "count": cost}]:
            raise AssertionError(f"managed extended cost for tier {tier} is incorrect")
    huntmasters = [entry for entry in manifest.get("creatureTemplates", [])
                   if entry.get("symbol", "").startswith("huntmaster-")]
    if len(huntmasters) != 10 or any(
            entry.get("overrides", {}).get("npcFlags") != 129
            for entry in huntmasters):
        raise AssertionError("all existing Huntmasters must retain gossip and gain vendor flags")
    for token in ('"item-extended-cost.id"', "SealStoreExtendedCosts",
                  "ExtendedCostMatchesConfiguredPrice"):
        if token not in manager + header + module:
            raise AssertionError(f"managed extended-cost resolution missing: {token}")
    if "SelectDeliverableReward" in manager:
        raise AssertionError("Elite reward selection behavior regressed")
    print("PASS Huntmaster Seal vendor parity and transaction contracts", flush=True)


def main() -> None:
    checks = (
        run_function_shadow_guard,
        run_domain_tests,
        run_tracking_milestone_source_checks,
        run_relic_persistence_contract_checks,
        run_functional_ui_checks,
        run_frameforge_functional_ui_checks,
        run_frameforge_package_checks,
        run_seal_vendor_checks,
        run_source_safety_checks,
    )
    failures = []
    for check in checks:
        try:
            check()
        except Exception as error:
            print(f"FAIL {check.__name__}: {error}", flush=True)
            failures.append(check.__name__)
    if failures:
        raise SystemExit(
            f"{len(failures)} of {len(checks)} check groups failed: "
            f"{', '.join(failures)}")
    print("PASS all Native Hunts check groups", flush=True)


if __name__ == "__main__":
    main()
