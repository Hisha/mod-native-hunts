local NATIVE_HUNTS_DUNGEON_TAB, NATIVE_HUNTS_HUNTS_TAB = 1, 2;
local PREFIX, VERSION, MAX_MESSAGE, MAX_FRAGMENTS, MAX_RECORD = "NHUNTS", "1", 248, 8, 1024;
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

-- Exact control and wrapper identities from frameforge-manifest.json/controlInventory.
-- State codes here are module-owned: I=Idle, T=Tracking, L=Located, F=Fight, R=Turnin.
local PRESENTATION = {
	{"Elite_Hunt_Icon", "Elite_Hunt_Icon__FFLayer", "TLFR"},
	{"Header_Text", "Header_Text__FFLayer", "*"},
	{"Hunt_Panel_Record", "Hunt_Panel_Record__FFLayer", "*"},
	{"Hunt_Panel_Idle", "Hunt_Panel_Idle__FFLayer", "I"},
	{"No_Active_Hunt", "No_Active_Hunt__FFLayer", "I"},
	{"Speak_Huntmaster", "Speak_Huntmaster__FFLayer", "I"},
	{"begin_hunt", "begin_hunt__FFLayer", "I"},
	{"Hunt_Seals", "Hunt_Seals__FFLayer", "*"},
	{"Elite_Today", "Elite_Today__FFLayer", "*"},
	{"Elite_Hunts", "Elite_Hunts__FFLayer", "*"},
	{"Standard_Hunts", "Standard_Hunts__FFLayer", "*"},
	{"Hunt_Seal", "Hunt_Seal__FFLayer", "*"},
	{"Hunt_Circle_Icon", "Hunt_Circle_Icon__FFLayer", "*"},
	{"Standard_Hunts_Value", "Standard_Hunts_Value__FFLayer", "*"},
	{"Elite_Hunts_Value", "Elite_Hunts_Value__FFLayer", "*"},
	{"Elite_Today_Available", "Elite_Today_Available__FFLayer", "*"},
	{"Elite_Today_Unavailable", "Elite_Today_Unavailable__FFLayer", "*"},
	{"Hunt_Seal_Value", "Hunt_Seal_Value__FFLayer", "*"},
	{"Hunt_Panel_Top", "Hunt_Panel_Top__FFLayer", "TLFR"},
	{"Hunt_Panel_Bottom", "Hunt_Panel_Bottom__FFLayer", "TLFR"},
	{"Hunt_Header", "Hunt_Header__FFLayer", "TLFR"},
	{"Standard_Hunt_Icon", "Standard_Hunt_Icon__FFLayer", "TLFR"},
	{"Hunt_Progress_Text", "Hunt_Progress_Text__FFLayer", "T"},
	{"Trail_Located_Text", "Trail_Located_Text__FFLayer", "L"},
	{"Final_Confrontation_Text", "Final_Confrontation_Text__FFLayer", "F"},
	{"Hunt_Complete_Text", "Hunt_Complete_Text__FFLayer", "R"},
	{"Tracker_Elite_Hunt", "Tracker_Elite_Hunt__FFLayer", "TLFR"},
	{"Tracker_Standard_Hunt", "Tracker_Standard_Hunt__FFLayer", "TLFR"},
	{"Tracker_Target_Name", "Tracker_Target_Name__FFLayer", "TLFR"},
	{"Tracker_Huntmaster", "Tracker_Huntmaster__FFLayer", "TLFR"},
	{"Tracker_Location", "Tracker_Location__FFLayer", "TLFR"},
	{"Tracker_Hunt_Ground", "Tracker_Hunt_Ground__FFLayer", "TLFR"},
	{"Tracker_Hunt_Ground_Value", "Tracker_Hunt_Ground_Value__FFLayer", "TLFR"},
	{"Tracker_Progress_Surround", "Tracker_Progress_Surround__FFLayer", "TL"},
	{"Tracker_Hunt_Progress", "Tracker_Hunt_Progress", "TL"},
	{"Tracking_Text", "Tracking_Text__FFLayer", "T"},
	{"Tracking2_Text", "Tracking2_Text__FFLayer", "T"},
	{"Tracking3_Text", "Tracking3_Text__FFLayer", "T"},
	{"Located_Text", "Located_Text__FFLayer", "L"},
	{"Located2_Text", "Located2_Text__FFLayer", "L"},
	{"Located3_Text", "Located3_Text__FFLayer", "L"},
	{"Hunt_Complete_Icon", "Hunt_Complete_Icon__FFLayer", "R"},
	{"Complete_Text", "Complete_Text__FFLayer", "R"},
	{"Complete2A_Text", "Complete2A_Text__FFLayer", "R"},
	{"Complete2B_Huntmaster_Value", "Complete2B_Huntmaster_Value__FFLayer", "R"},
	{"Complete3A_Text", "Complete3A_Text__FFLayer", "R"},
	{"Complete3B_Location_Value", "Complete3B_Location_Value__FFLayer", "R"},
	{"Final_Fight_Icon", "Final_Fight_Icon__FFLayer", "F"},
	{"Final_Fight_Text", "Final_Fight_Text__FFLayer", "F"},
};
local presentationFrame = {};
for _, entry in ipairs(PRESENTATION) do presentationFrame[entry[1]] = entry[2]; end

local function SetPresentationVisible(name, visible)
	local control = _G[presentationFrame[name]];
	if not control then return; end
	if visible then control:Show(); else control:Hide(); end
end

local function ApplyVisualState(state)
	for _, entry in ipairs(PRESENTATION) do
		local visible = entry[3] == "*" or string.find(entry[3], state, 1, true) ~= nil;
		local control = _G[entry[2]];
		if control then if visible then control:Show(); else control:Hide(); end end
	end
end

local RECORD_CONTROLS = {"Hunt_Panel_Record", "Hunt_Seals", "Elite_Today", "Elite_Hunts",
	"Standard_Hunts", "Hunt_Seal", "Hunt_Circle_Icon", "Standard_Hunts_Value",
	"Elite_Hunts_Value", "Elite_Today_Available", "Elite_Today_Unavailable", "Hunt_Seal_Value"};

local function SetRecordVisible(visible)
	for _, name in ipairs(RECORD_CONTROLS) do SetPresentationVisible(name, visible); end
end

local function RenderWaiting(text)
	ApplyVisualState("I");
	SetRecordVisible(false);
	No_Active_Hunt:SetText(text);
	SetPresentationVisible("Speak_Huntmaster", false);
	SetPresentationVisible("begin_hunt", false);
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

local function RenderRecord(stats)
	SetRecordVisible(true);
	Standard_Hunts_Value:SetText(tostring(stats.standard or 0));
	Elite_Hunts_Value:SetText(tostring(stats.elite or 0));
	Elite_Today_Available:SetText("Available");
	Elite_Today_Unavailable:SetText(stats.eliteUnlocked and "Unavailable" or "Locked");
	SetPresentationVisible("Elite_Today_Available", stats.eliteUnlocked and stats.eliteAvailable);
	SetPresentationVisible("Elite_Today_Unavailable", not stats.eliteUnlocked or not stats.eliteAvailable);
	if stats.sealState == "A" then
		SetPresentationVisible("Hunt_Seal", true);
		Hunt_Seal_Value:SetText(tostring(stats.seals or 0));
	else
		SetPresentationVisible("Hunt_Seal", false);
		Hunt_Seal_Value:SetText("Unavailable");
	end
end

local function Render(snapshot)
	if not snapshot or not snapshot.stats then RenderWaiting("Hunt information unavailable."); return; end
	if not snapshot.contentAvailable then
		ApplyVisualState("I"); RenderRecord(snapshot.stats);
		No_Active_Hunt:SetText("Hunt Information Unavailable");
		Speak_Huntmaster:SetText(Display(snapshot.reason, "Native Hunts content is unavailable."));
		begin_hunt:SetText(""); return;
	end
	if not snapshot.active then
		ApplyVisualState("I"); RenderRecord(snapshot.stats);
		No_Active_Hunt:SetText("No Active Hunt");
		Speak_Huntmaster:SetText("Speak with a Huntmaster");
		begin_hunt:SetText("to begin a Hunt.");
		return;
	end
	local huntmaster = Display(snapshot.huntmaster, "Unknown Huntmaster");
	local city = Display(snapshot.city, "Unknown Location");
	local prey = Display(snapshot.prey, "Unknown Quarry");
	local zone = Display(snapshot.zone, "Unknown Hunting Ground");
	local finalLocation = Display(snapshot.finalLocation, zone);
	local state = snapshot.state == "T" and "T" or snapshot.state == "F" and "L" or
		snapshot.state == "P" and "F" or snapshot.state == "R" and "R" or "I";
	ApplyVisualState(state); RenderRecord(snapshot.stats);
	if state == "I" then
		No_Active_Hunt:SetText("Hunt Status Unavailable");
		Speak_Huntmaster:SetText("The authoritative Hunt state could not be displayed.");
		begin_hunt:SetText(""); return;
	end
	if snapshot.tier == "E" then
		SetPresentationVisible("Elite_Hunt_Icon", true);
		SetPresentationVisible("Tracker_Elite_Hunt", true);
		SetPresentationVisible("Standard_Hunt_Icon", false);
		SetPresentationVisible("Tracker_Standard_Hunt", false);
	elseif snapshot.tier == "S" then
		SetPresentationVisible("Elite_Hunt_Icon", false);
		SetPresentationVisible("Tracker_Elite_Hunt", false);
		SetPresentationVisible("Standard_Hunt_Icon", true);
		SetPresentationVisible("Tracker_Standard_Hunt", true);
	end
	Tracker_Target_Name:SetText(prey);
	Tracker_Huntmaster:SetText(huntmaster);
	Tracker_Location:SetText(city);
	Tracker_Hunt_Ground_Value:SetText(snapshot.state == "F" and finalLocation or zone);
	Complete2B_Huntmaster_Value:SetText(huntmaster);
	Complete3B_Location_Value:SetText(city);
	Tracker_Hunt_Progress:SetMinMaxValues(0, 100);
	Tracker_Hunt_Progress:SetValue(Progress(snapshot.progress));
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
	if not LFDParentFrame or not LFDQueueFrame or not NativeHuntsFrame or not FrameForge_Dungeon_Finder_UI or
			not Tracker_Hunt_Progress or not LFDParentFrameTab1 or not LFDParentFrameTab2 then return; end
	Tracker_Hunt_Progress:SetMinMaxValues(0, 100);
	Tracker_Hunt_Progress:SetValue(0);
	RenderWaiting("Retrieving Hunt information...");
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
	LFDParentFrame.numTabs = 2;

	if PanelTemplates_SetNumTabs then
	    PanelTemplates_SetNumTabs(LFDParentFrame, 2);
	end

	if PanelTemplates_TabResize then
	    PanelTemplates_TabResize(LFDParentFrameTab1, 0);
	    PanelTemplates_TabResize(LFDParentFrameTab2, 0);
	end
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
