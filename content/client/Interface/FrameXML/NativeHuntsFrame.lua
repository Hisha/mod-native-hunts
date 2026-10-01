local NATIVE_HUNTS_DUNGEON_TAB, NATIVE_HUNTS_HUNTS_TAB = 1, 2;
local PREFIX, VERSION, MAX_MESSAGE, MAX_FRAGMENTS, MAX_RECORD = "NHUNTS", "1", 248, 8, 1024;
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
	if string.find(string.gsub(value, "%%[%x][%x]", ""), "%%") then return nil; end
	local decoded = string.gsub(value, "%%(%x%x)", function(hex) return string.char(tonumber(hex, 16)); end);
	if string.find(decoded, "[\000-\008\011\012\014-\031]") then return nil; end
	return decoded;
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

local function RenderWaiting(text)
	NativeHuntsFrameContentPanelTier:SetText("");
	NativeHuntsFrameContentPanelState:SetText(text);
	NativeHuntsFrameContentPanelDescription:SetText("");
	NativeHuntsFrameContentPanelProgressLabel:SetText("");
	NativeHuntsFrameContentPanelProgress:Hide();
	NativeHuntsFrameContentPanelFinal:SetText("");
	NativeHuntsFrameContentPanelStats:SetText("");
end

local function Render(snapshot)
	local stats, eliteStatus = snapshot.stats, "Locked";
	if stats.eliteUnlocked then eliteStatus = stats.eliteAvailable and "Available" or "Unavailable today"; end
	local seals = stats.sealState == "A" and tostring(stats.seals) or "Unavailable";
	NativeHuntsFrameContentPanelStats:SetText("Standard: " .. stats.standard .. "   Elite: " .. stats.elite ..
		"\nElite: " .. eliteStatus .. "   Seals: " .. seals);
	if not snapshot.contentAvailable then
		RenderWaiting("Hunt information unavailable.");
		NativeHuntsFrameContentPanelDescription:SetText(snapshot.reason or "");
		NativeHuntsFrameContentPanelStats:SetText("Huntmaster's Seals: " .. seals); return;
	end
	if not snapshot.active then
		NativeHuntsFrameContentPanelTier:SetText(""); NativeHuntsFrameContentPanelState:SetText("No Active Hunt");
		NativeHuntsFrameContentPanelDescription:SetText("Speak with a Huntmaster to begin a Hunt.");
		NativeHuntsFrameContentPanelProgressLabel:SetText(""); NativeHuntsFrameContentPanelProgress:Hide();
		NativeHuntsFrameContentPanelFinal:SetText(""); return;
	end
	NativeHuntsFrameContentPanelTier:SetText(snapshot.tier == "E" and "ELITE HUNT" or "STANDARD HUNT");
	NativeHuntsFrameContentPanelState:SetText(snapshot.prey);
	NativeHuntsFrameContentPanelDescription:SetText("Huntmaster: " .. snapshot.huntmaster .. "\n" .. snapshot.city ..
		"\nHunting Ground: " .. snapshot.zone);
	NativeHuntsFrameContentPanelProgressLabel:SetText("Tracking"); NativeHuntsFrameContentPanelProgress:Show();
	NativeHuntsFrameContentPanelProgress:SetValue(snapshot.progress);
	NativeHuntsFrameContentPanelProgressText:SetText(snapshot.progress .. "%");
	if snapshot.state == "R" and snapshot.ready then
		NativeHuntsFrameContentPanelFinal:SetText("|cff20ff20READY TO TURN IN|r\nReturn to " .. snapshot.huntmaster .. "\n" .. snapshot.city);
	elseif snapshot.state == "P" then NativeHuntsFrameContentPanelFinal:SetText("|cffffd200Prey Engaged|r");
	elseif snapshot.finalVisible then NativeHuntsFrameContentPanelFinal:SetText("Final Location\n" .. snapshot.finalLocation);
	else NativeHuntsFrameContentPanelFinal:SetText("Follow the trail through the hunting ground."); end
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
	nonce=math.mod(nonce,2147483646)+1; pendingNonce=nonce;
	SendAddonMessage(PREFIX,"1\tQ\t"..nonce,"WHISPER",UnitName("player"));
	if not hasSnapshot then RenderWaiting("Retrieving Hunt information..."); end
	if timeoutGroup then timeoutGroup:Stop(); timeoutGroup:Play(); end
end

function NativeHuntsFrame_SelectTab(tab)
	if not LFDParentFrame or not LFDQueueFrame or not NativeHuntsFrame then return; end
	if tab==NATIVE_HUNTS_HUNTS_TAB then LFDQueueFrame:Hide(); NativeHuntsFrame:Show(); RequestSnapshot();
	else tab=NATIVE_HUNTS_DUNGEON_TAB; NativeHuntsFrame:Hide(); LFDQueueFrame:Show(); end
	NativeHuntsFrame_SetTitle(); if PanelTemplates_SetTab then PanelTemplates_SetTab(LFDParentFrame,tab); end
end

function NativeHuntsFrame_OnLoad(self)
	if not LFDParentFrame or not LFDQueueFrame or not NativeHuntsFrame or not LFDParentFrameTab1 or not LFDParentFrameTab2 then return; end
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
	LFDParentFrame:HookScript("OnShow",function() if NativeHuntsFrame:IsShown() then RequestSnapshot(); end end);
end

function NativeHuntsFrame_OnEvent(self,event,...)
	if event=="PLAYER_ENTERING_WORLD" then RequestSnapshot(); return; end
	local prefix,message,channel,sender=...;
	if event=="CHAT_MSG_ADDON" and prefix==PREFIX and channel=="WHISPER" and sender==UnitName("player") then HandleMessage(message); end
end
