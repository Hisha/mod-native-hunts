#!/usr/bin/env python3
"""Compile and run Native Hunts domain, gameplay, and content checks."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import json
import hashlib
import re
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
            "src/HuntSnapshot.cpp", "src/HuntUiProtocol.cpp", "src/HuntCatalog.cpp", "src/HuntRewards.cpp", "-o", str(executable)]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(executable)], cwd=ROOT, check=True)
        print("PASS hunt domain, gameplay, recovery, and snapshot tests", flush=True)


def run_content_checks() -> None:
    epf = ROOT / "content" / "mod-native-hunts.epf"
    manifest_source = (ROOT / "content" / "manifest.json").read_bytes()
    source_manifest = json.loads(manifest_source)
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
    if (manifest.get("package") != "mod-native-hunts" or
            manifest.get("schema") != 3 or manifest.get("version") != "17"):
        raise AssertionError("invalid Native Hunts EPF identity")
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
    print("PASS managed Native Hunts EPF and passive FrameXML contract", flush=True)


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
