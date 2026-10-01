local NATIVE_HUNTS_DUNGEON_TAB = 1;
local NATIVE_HUNTS_HUNTS_TAB = 2;

local function NativeHuntsFrame_SetTitle()
	if ( LFDQueueFrameTitleText ) then
		LFDQueueFrameTitleText:SetText("Player vs Environment");
	end
end

function NativeHuntsFrame_SelectTab(tab)
	if ( not LFDParentFrame or not LFDQueueFrame or not NativeHuntsFrame ) then
		return;
	end

	if ( tab == NATIVE_HUNTS_HUNTS_TAB ) then
		LFDQueueFrame:Hide();
		NativeHuntsFrame:Show();
	else
		tab = NATIVE_HUNTS_DUNGEON_TAB;
		NativeHuntsFrame:Hide();
		LFDQueueFrame:Show();
	end

	NativeHuntsFrame_SetTitle();
	if ( PanelTemplates_SetTab ) then
		PanelTemplates_SetTab(LFDParentFrame, tab);
	end
end

function NativeHuntsFrame_OnLoad(self)
	if ( not LFDParentFrame or not LFDQueueFrame or not NativeHuntsFrame or
			not LFDParentFrameTab1 or not LFDParentFrameTab2 ) then
		return;
	end

	if ( PanelTemplates_SetNumTabs and PanelTemplates_TabResize ) then
		PanelTemplates_SetNumTabs(LFDParentFrame, 2);
		PanelTemplates_TabResize(LFDParentFrameTab1, 0);
		PanelTemplates_TabResize(LFDParentFrameTab2, 0);
	end
	NativeHuntsFrame_SelectTab(NATIVE_HUNTS_DUNGEON_TAB);

	if ( LFDMicroButton and MicroButtonTooltipText ) then
		LFDMicroButton.tooltipText = MicroButtonTooltipText("Player vs Environment", "TOGGLELFGPARENT");
		LFDMicroButton.newbieText = "Find a dungeon group or review your Native Hunts.";
	end

	if ( hooksecurefunc and type(LFDFrame_OnEvent) == "function" ) then
		hooksecurefunc("LFDFrame_OnEvent", function(frame, event)
			if ( event == "LFG_OPEN_FROM_GOSSIP" ) then
				NativeHuntsFrame_SelectTab(NATIVE_HUNTS_DUNGEON_TAB);
			end
		end);
	end
end
