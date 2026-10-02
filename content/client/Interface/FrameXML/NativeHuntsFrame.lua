local NATIVE_HUNTS_DUNGEON_TAB, NATIVE_HUNTS_HUNTS_TAB = 1, 2;
local PREFIX, VERSION, MAX_MESSAGE, MAX_FRAGMENTS, MAX_RECORD = "NHUNTS", "1", 248, 8, 1024;
local STANDARD_ICON = "Interface\\NativeHunts\\hunt_icon_standard.tga";
local ELITE_ICON = "Interface\\NativeHunts\\hunt_icon_elite.tga";
local STOCK_PARENT_WIDTH, STOCK_PARENT_HEIGHT = 355, 440;
local HUNTS_PARENT_WIDTH, HUNTS_PARENT_HEIGHT = 355, 500;
local parentWidth, parentHeight = STOCK_PARENT_WIDTH, STOCK_PARENT_HEIGHT;
local parentCloseButton, parentCloseAnchor = nil, nil;
local nonce, pendingNonce, lastSequence = 0, nil, 0;
local assembly, hasSnapshot, timeoutGroup = nil, false, nil;
local lastRequestAt = -100;

local function Split(value, limit)
	local fields, start = {}, 1;
	while (not limit or table.getn(fields) < limit - 1) do
		local at = string.find(value, "\t", start, true);
		if not at then break; end
		table.insert(fields, string.sub(value, start, at - 1)); start = at + 1;
	end
	table.insert(fields, string.sub(value, start)); return fields;
end

local function Decode(value)
	if string.len(value) > MAX_RECORD then return nil; end
	local decoded, index = {}, 1;
	while index <= string.len(value) do
		local byte = string.byte(value, index);
		if byte == 37 then
			local hex = string.sub(value, index + 1, index + 2);
			if string.len(hex) ~= 2 or not string.find(hex, "^%x%x$") then return nil; end
			byte = tonumber(hex, 16); index = index + 3;
		else index = index + 1; end
		if byte < 9 or byte == 11 or byte == 12 or (byte >= 14 and byte <= 31) then return nil; end
		table.insert(decoded, string.char(byte));
	end
	return table.concat(decoded);
end

local function Number(value, maximum)
	if not value or not string.find(value, "^%d+$") then return nil; end
	local result = tonumber(value);
	if not result or result < 0 or result > maximum then return nil; end
	return result;
end

local function NativeHuntsFrame_SetTitle()
	if LFDQueueFrameTitleText then LFDQueueFrameTitleText:SetText("Player vs. Environment"); end
end

local function NativeHuntsFrame_UpdateMicroButtonTooltip(button)
	if not button or not MicroButtonTooltipText then return; end
	button.tooltipText = MicroButtonTooltipText("Player vs. Environment", "TOGGLELFGPARENT");
	button.newbieText = "Find a dungeon group or review your Native Hunts.";
end

local function ClearActivePresentation()
	NativeHuntsFrameContentPanelIdentity:Hide();
	NativeHuntsFrameContentPanelHuntState:Hide();
	NativeHuntsFrameContentPanelIdle:Hide();
	NativeHuntsFrameContentPanelIdentityIcon:SetTexture(nil);
	NativeHuntsFrameContentPanelIdentityTier:SetText("");
	NativeHuntsFrameContentPanelIdentityPrey:SetText("");
	NativeHuntsFrameContentPanelIdentityIssuer:SetText("");
	NativeHuntsFrameContentPanelHuntStateHeader:SetText("");
	NativeHuntsFrameContentPanelHuntStatePrimary:SetText("");
	NativeHuntsFrameContentPanelHuntStateSecondary:SetText("");
	NativeHuntsFrameContentPanelHuntStateProgress:SetValue(0);
	NativeHuntsFrameContentPanelHuntStateProgress:Hide();
	NativeHuntsFrameContentPanelHuntStateProgressText:SetText("");
	NativeHuntsFrameContentPanelHuntStateDecoration:Hide();
	NativeHuntsFrameContentPanelHuntStateReadyIcon:Hide();
	NativeHuntsFrameContentPanelIdleState:SetText("");
	NativeHuntsFrameContentPanelIdleDescription:SetText("");
end

local function RenderWaiting(text)
	ClearActivePresentation();
	NativeHuntsFrameContentPanelRecord:Hide();
	NativeHuntsFrameContentPanelIdle:Show();
	NativeHuntsFrameContentPanelIdleState:SetText(text);
end

local function Display(value, fallback)
	if not value or value == "" then return fallback; end
	return value;
end

local function Progress(value)
	local progress = tonumber(value) or 0;
	if progress < 0 then return 0; end
	if progress > 100 then return 100; end
	return progress;
end

local function Place(control, point, relativePoint, x, y)
	control:ClearAllPoints();
	control:SetPoint(point, NativeHuntsFrameContentPanelHuntState, relativePoint, x, y);
end

local function RenderRecord(stats)
	local availability = "|cff888888Locked|r";
	if stats.eliteUnlocked then
		availability = stats.eliteAvailable and "|cff20ff20Available|r" or "|cff888888Unavailable|r";
	end
	NativeHuntsFrameContentPanelRecordStandard:SetText(tostring(stats.standard or 0));
	NativeHuntsFrameContentPanelRecordElite:SetText(tostring(stats.elite or 0));
	NativeHuntsFrameContentPanelRecordAvailability:SetText(availability);
	if stats.sealState == "A" then
		NativeHuntsFrameContentPanelRecordSealIcon:Show();
		NativeHuntsFrameContentPanelRecordSeals:SetText(tostring(stats.seals or 0));
	else
		NativeHuntsFrameContentPanelRecordSealIcon:Hide();
		NativeHuntsFrameContentPanelRecordSeals:SetText("Unavailable");
	end
	NativeHuntsFrameContentPanelRecord:Show();
end

local function Render(snapshot)
	if not snapshot or not snapshot.stats then RenderWaiting("Hunt information unavailable."); return; end
	ClearActivePresentation();
	RenderRecord(snapshot.stats);
	if not snapshot.contentAvailable then
		NativeHuntsFrameContentPanelIdle:Show();
		NativeHuntsFrameContentPanelIdleState:SetText("Hunt information unavailable.");
		NativeHuntsFrameContentPanelIdleDescription:SetText(snapshot.reason or ""); return;
	end
	if not snapshot.active then
		NativeHuntsFrameContentPanelIdle:Show();
		NativeHuntsFrameContentPanelIdleState:SetText("NO ACTIVE HUNT");
		NativeHuntsFrameContentPanelIdleDescription:SetText("Speak with a Huntmaster\nto begin a Hunt.");
		return;
	end
	local huntmaster = Display(snapshot.huntmaster, "Unknown Huntmaster");
	local city = Display(snapshot.city, "Unknown Location");
	local prey = Display(snapshot.prey, "Unknown Quarry");
	local zone = Display(snapshot.zone, "Unknown Hunting Ground");
	local finalLocation = Display(snapshot.finalLocation, zone);
	NativeHuntsFrameContentPanelIdentity:Show();
	NativeHuntsFrameContentPanelHuntState:Show();
	if snapshot.tier == "E" then
		NativeHuntsFrameContentPanelIdentityIcon:SetTexture(ELITE_ICON);
		NativeHuntsFrameContentPanelIdentityTier:SetText("ELITE HUNT");
	elseif snapshot.tier == "S" then
		NativeHuntsFrameContentPanelIdentityIcon:SetTexture(STANDARD_ICON);
		NativeHuntsFrameContentPanelIdentityTier:SetText("STANDARD HUNT");
	end
	NativeHuntsFrameContentPanelIdentityPrey:SetText(prey);
	NativeHuntsFrameContentPanelIdentityIssuer:SetText(huntmaster .. "  |cff9d9d9d•|r  " .. city);
	if snapshot.state == "T" then
		local progress = Progress(snapshot.progress);
		Place(NativeHuntsFrameContentPanelHuntStatePrimary, "TOPLEFT", "TOPLEFT", 15, -34);
		Place(NativeHuntsFrameContentPanelHuntStateSecondary, "TOPLEFT", "TOPLEFT", 15, -78);
		NativeHuntsFrameContentPanelHuntStateHeader:SetText("HUNT PROGRESS");
		NativeHuntsFrameContentPanelHuntStatePrimary:SetText("Tracking");
		NativeHuntsFrameContentPanelHuntStateProgress:SetValue(progress);
		NativeHuntsFrameContentPanelHuntStateProgressText:SetText(progress .. "%");
		NativeHuntsFrameContentPanelHuntStateProgress:Show();
		NativeHuntsFrameContentPanelHuntStateDecoration:Show();
		NativeHuntsFrameContentPanelHuntStateSecondary:SetText("Follow the trail through |cffffd200" .. zone .. "|r.");
	elseif snapshot.state == "F" then
		local progress = Progress(snapshot.progress);
		Place(NativeHuntsFrameContentPanelHuntStatePrimary, "TOPLEFT", "TOPLEFT", 15, -64);
		Place(NativeHuntsFrameContentPanelHuntStateSecondary, "TOPLEFT", "TOPLEFT", 15, -92);
		NativeHuntsFrameContentPanelHuntStateHeader:SetText("TRAIL LOCATED");
		NativeHuntsFrameContentPanelHuntStatePrimary:SetText("Final Location\n|cffffd200" .. finalLocation .. "|r");
		NativeHuntsFrameContentPanelHuntStateProgress:SetValue(progress);
		NativeHuntsFrameContentPanelHuntStateProgressText:SetText(progress .. "%");
		NativeHuntsFrameContentPanelHuntStateProgress:Show();
		NativeHuntsFrameContentPanelHuntStateSecondary:SetText("Travel to the marked location and use\nthe Prey Trail Crystal.");
	elseif snapshot.state == "P" then
		Place(NativeHuntsFrameContentPanelHuntStatePrimary, "TOPLEFT", "TOPLEFT", 15, -48);
		Place(NativeHuntsFrameContentPanelHuntStateSecondary, "TOPLEFT", "TOPLEFT", 15, -77);
		NativeHuntsFrameContentPanelHuntStateHeader:SetText("FINAL CONFRONTATION");
		NativeHuntsFrameContentPanelHuntStatePrimary:SetText("|cffffd200" .. prey .. "|r");
		NativeHuntsFrameContentPanelHuntStateSecondary:SetText("Defeat your prey.");
	elseif snapshot.state == "R" then
		Place(NativeHuntsFrameContentPanelHuntStatePrimary, "TOPLEFT", "TOPLEFT", 15, -43);
		Place(NativeHuntsFrameContentPanelHuntStateSecondary, "TOPLEFT", "TOPLEFT", 15, -75);
		NativeHuntsFrameContentPanelHuntStateHeader:SetText("HUNT COMPLETE");
		NativeHuntsFrameContentPanelHuntStatePrimary:SetText("|cffffd200READY TO TURN IN|r");
		NativeHuntsFrameContentPanelHuntStateSecondary:SetText("Return to " .. huntmaster .. "\nin " .. city .. ".");
		NativeHuntsFrameContentPanelHuntStateReadyIcon:Show();
	else
		Place(NativeHuntsFrameContentPanelHuntStatePrimary, "TOPLEFT", "TOPLEFT", 15, -48);
		Place(NativeHuntsFrameContentPanelHuntStateSecondary, "TOPLEFT", "TOPLEFT", 15, -77);
		NativeHuntsFrameContentPanelHuntStateHeader:SetText("HUNT STATUS");
		NativeHuntsFrameContentPanelHuntStatePrimary:SetText("Hunt information unavailable.");
		NativeHuntsFrameContentPanelHuntStateSecondary:SetText("The authoritative Hunt state could not be displayed.");
	end
end

local function ParseAssignment(body)
	local f = Split(body); if table.getn(f) ~= 14 then return nil; end
	local revision, progress = Number(f[1], 9007199254740991), Number(f[6], 100);
	if not revision or not progress or not string.find(f[4], "^[ITFPR]$") or not string.find(f[5], "^[NSE]$") then return nil; end
	if not string.find(f[2], "^[01]$") or not string.find(f[3], "^[01]$") or
			not string.find(f[7], "^[01]$") or not string.find(f[8], "^[01]$") then return nil; end
	local decoded = {}; for index = 9, 14 do decoded[index] = Decode(f[index]); if decoded[index] == nil then return nil; end end
	return {revision=revision, contentAvailable=f[2]=="1", active=f[3]=="1", state=f[4], tier=f[5], progress=progress,
		finalVisible=f[7]=="1", ready=f[8]=="1", huntmaster=decoded[9], city=decoded[10], prey=decoded[11],
		zone=decoded[12], finalLocation=decoded[13], reason=decoded[14]};
end

local function ParseProgression(body)
	local f = Split(body); if table.getn(f) ~= 8 or not string.find(f[7], "^[AU]$") then return nil; end
	if not string.find(f[3], "^[01]$") or not string.find(f[6], "^[01]$") then return nil; end
	local standard, elite = Number(f[1],4294967295), Number(f[2],4294967295);
	local accepted, limit, seals = Number(f[4],4294967295), Number(f[5],4294967295), Number(f[8],4294967295);
	if not standard or not elite or not accepted or not limit or not seals then return nil; end
	return {standard=standard, elite=elite, eliteUnlocked=f[3]=="1", accepted=accepted, limit=limit,
		eliteAvailable=f[6]=="1", sealState=f[7], seals=seals};
end

local function AcceptRecord(sequence, responseNonce, kind, body)
	if sequence <= lastSequence or (responseNonce ~= 0 and responseNonce ~= pendingNonce) then return; end
	if not assembly or assembly.sequence ~= sequence or assembly.nonce ~= responseNonce then assembly={sequence=sequence,nonce=responseNonce}; end
	if kind == "A" then assembly.assignment=ParseAssignment(body); elseif kind == "P" then assembly.stats=ParseProgression(body); else return; end
	if not assembly.assignment or not assembly.stats then return; end
	assembly.assignment.stats=assembly.stats; lastSequence=sequence; hasSnapshot=true;
	if responseNonce == pendingNonce then pendingNonce=nil; end
	if timeoutGroup and timeoutGroup:IsPlaying() then timeoutGroup:Stop(); end
	Render(assembly.assignment); assembly=nil;
end

local function HandleMessage(message)
	if not message or string.len(message)>MAX_MESSAGE then return; end
	local f=Split(message,8); if f[1]~=VERSION then return; end
	local sequence, responseNonce=Number(f[3],4294967295),Number(f[4],4294967295);
	if not sequence or sequence==0 or responseNonce==nil then return; end
	if f[2]=="A" or f[2]=="P" then if table.getn(f)<5 then return; end AcceptRecord(sequence,responseNonce,f[2],table.concat(f,"\t",5)); return; end
	if f[2]~="F" or table.getn(f)~=8 or (f[5]~="A" and f[5]~="P") then return; end
	local part,count=Number(f[6],MAX_FRAGMENTS),Number(f[7],MAX_FRAGMENTS);
	if not part or not count or part<1 or count<1 or part>count or sequence<=lastSequence then return; end
	if responseNonce~=0 and responseNonce~=pendingNonce then return; end
	if not assembly or assembly.sequence~=sequence or assembly.nonce~=responseNonce then assembly={sequence=sequence,nonce=responseNonce,fragments={}}; end
	if not assembly.fragments then assembly.fragments={}; end
	local key=f[5]; local group=assembly.fragments[key];
	if not group or group.count~=count then group={count=count,parts={}}; assembly.fragments[key]=group; end
	group.parts[part]=f[8]; local pieces={};
	for index=1,count do if not group.parts[index] then return; else pieces[index]=group.parts[index]; end end
	local body=table.concat(pieces); if string.len(body)>MAX_RECORD then assembly=nil; return; end
	AcceptRecord(sequence,responseNonce,key,body);
end

local function RequestSnapshot()
	if not SendAddonMessage or not UnitName("player") then return; end
	local now = GetTime and GetTime() or 0;
	if now - lastRequestAt < 1 then return; end
	lastRequestAt = now;
	nonce=nonce%2147483646+1; pendingNonce=nonce;
	SendAddonMessage(PREFIX,"1\tQ\t"..nonce,"WHISPER",UnitName("player"));
	if not hasSnapshot then RenderWaiting("Retrieving Hunt information..."); end
	if timeoutGroup then timeoutGroup:Stop(); timeoutGroup:Play(); end
end

local function SizeParent(width, height)
	LFDParentFrame:SetWidth(width);
	LFDParentFrame:SetHeight(height);
end

local function PositionTabs()
	LFDParentFrameTab1:ClearAllPoints();
	LFDParentFrameTab1:SetPoint("BOTTOMLEFT", LFDParentFrame, "BOTTOMLEFT", 18, -27);
	LFDParentFrameTab2:ClearAllPoints();
	LFDParentFrameTab2:SetPoint("LEFT", LFDParentFrameTab1, "RIGHT", -15, 0);
end

local function PositionCloseButton()
	if not parentCloseButton or not parentCloseAnchor then return; end
	parentCloseButton:ClearAllPoints();
	parentCloseButton:SetPoint(parentCloseAnchor[1], parentCloseAnchor[2], parentCloseAnchor[3], parentCloseAnchor[4], parentCloseAnchor[5]);
end

local function ApplyStockGeometry()
	SizeParent(parentWidth, parentHeight);
	PositionCloseButton();
	PositionTabs();
end

local function ApplyHuntsGeometry()
	SizeParent(HUNTS_PARENT_WIDTH, HUNTS_PARENT_HEIGHT);
	PositionCloseButton();
	PositionTabs();
end

function NativeHuntsFrame_SelectTab(tab)
	if not LFDParentFrame or not LFDQueueFrame or not NativeHuntsFrame then return; end
	if tab==NATIVE_HUNTS_HUNTS_TAB then ApplyHuntsGeometry(); LFDQueueFrame:Hide(); NativeHuntsFrame:Show(); RequestSnapshot();
	else tab=NATIVE_HUNTS_DUNGEON_TAB; ApplyStockGeometry(); NativeHuntsFrame:Hide(); LFDQueueFrame:Show(); end
	NativeHuntsFrame_SetTitle(); if PanelTemplates_SetTab then PanelTemplates_SetTab(LFDParentFrame,tab); end
end

function NativeHuntsFrame_OnLoad(self)
	if not LFDParentFrame or not LFDQueueFrame or not NativeHuntsFrame or not LFDParentFrameTab1 or not LFDParentFrameTab2 then return; end
	local width, height = LFDParentFrame:GetWidth(), LFDParentFrame:GetHeight();
	if width and width > 0 then parentWidth = width; end
	if height and height > 0 then parentHeight = height; end
	local children = {LFDParentFrame:GetChildren()};
	for index = 1, table.getn(children) do
		local child = children[index];
		if child ~= LFDQueueFrame and child ~= NativeHuntsFrame and child ~= LFDParentFrameTab1 and
				child ~= LFDParentFrameTab2 and child ~= LFDParentFramePortrait and
				child.GetObjectType and child:GetObjectType() == "Button" then
			parentCloseButton = child; break;
		end
	end
	if parentCloseButton then parentCloseAnchor = {parentCloseButton:GetPoint(1)}; end
	if RegisterAddonMessagePrefix then RegisterAddonMessagePrefix(PREFIX); end
	self:RegisterEvent("PLAYER_ENTERING_WORLD"); self:RegisterEvent("CHAT_MSG_ADDON");
	timeoutGroup=NativeHuntsFrame:CreateAnimationGroup(); local timeout=timeoutGroup:CreateAnimation("Alpha");
	timeout:SetChange(0); timeout:SetDuration(5); timeout:SetOrder(1);
	timeoutGroup:SetScript("OnFinished",function()
		if pendingNonce then pendingNonce=nil; hasSnapshot=false; assembly=nil; RenderWaiting("Hunt information unavailable."); end
	end);
	if PanelTemplates_SetNumTabs and PanelTemplates_TabResize then PanelTemplates_SetNumTabs(LFDParentFrame,2); PanelTemplates_TabResize(LFDParentFrameTab1,0); PanelTemplates_TabResize(LFDParentFrameTab2,0); end
	NativeHuntsFrame_SelectTab(NATIVE_HUNTS_DUNGEON_TAB);
	if LFDMicroButton then NativeHuntsFrame_UpdateMicroButtonTooltip(LFDMicroButton);
		if LFDMicroButton.HookScript then LFDMicroButton:HookScript("OnEvent",function(button,event) if event=="UPDATE_BINDINGS" then NativeHuntsFrame_UpdateMicroButtonTooltip(button); end end); end end
	if hooksecurefunc and type(LFDFrame_OnEvent)=="function" then hooksecurefunc("LFDFrame_OnEvent",function(frame,event) if event=="LFG_OPEN_FROM_GOSSIP" then NativeHuntsFrame_SelectTab(NATIVE_HUNTS_DUNGEON_TAB); end end); end
	LFDParentFrame:HookScript("OnShow",function()
		if NativeHuntsFrame:IsShown() then ApplyHuntsGeometry(); RequestSnapshot(); else ApplyStockGeometry(); end
	end);
	LFDParentFrame:HookScript("OnHide",ApplyStockGeometry);
end

function NativeHuntsFrame_OnEvent(self,event,...)
	if event=="PLAYER_ENTERING_WORLD" then RequestSnapshot(); return; end
	local prefix,message,channel,sender=...;
	if event=="CHAT_MSG_ADDON" and prefix==PREFIX and channel=="WHISPER" and sender==UnitName("player") then HandleMessage(message); end
end
