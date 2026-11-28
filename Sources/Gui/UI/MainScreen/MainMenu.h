/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include <Gui/UI/MainScreen/DemoListView.h>
#include <Gui/UI/MainScreen/EditorBrowserPanel.h>
#include <Gui/UI/MainScreen/ModListView.h>
#include <Gui/UI/MainScreen/ServerListView.h>
#include <Gui/UI/Widgets/Button.h>
#include <Gui/UI/Widgets/Field.h>
#include <Gui/UI/Widgets/Label.h>
#include <Gui/UI/Widgets/TabStrip.h>
#include <Gui/TrainingHost.h>

namespace spades {
	namespace gui {
		class MainScreenUI;
		class MainScreenHelper;
		class ModsScreenHelper;
		class ModPreviewCache;

		/** A `SimpleButton` decorated with the refresh arrow icon. */
		class RefreshButton : public ui::SimpleButton {
		public:
			RefreshButton(ui::UIManager* manager) : ui::SimpleButton(manager) {}
			void Render() override;
		};

		/** A toggle `SimpleButton` used for protocol selection/filtering. */
		class ProtocolButton : public ui::SimpleButton {
		public:
			ProtocolButton(ui::UIManager* manager) : ui::SimpleButton(manager) { toggle = true; }
		};

		/** Transparent container panel used for tab switching. */
		class TabPanel : public ui::UIElement {
		public:
			TabPanel(ui::UIManager* manager) : ui::UIElement(manager) {}
		};

		/**
		 * The part of the main menu that survives a rebuild. Everything else either
		 * lives in config (the quick-connect address, the protocol choice) or in the
		 * helper (the server list), and comes back on its own.
		 */
		struct MainScreenMainMenuState {
			/** -1 the home screen; 0 servers, 1 demos, 2 mods. */
			int activeTabIndex = -1;
			/** On the home screen's Singleplayer page, with this mode chosen. */
			bool singleplayer = false;
			int singleplayerMode = 0;
			bool singleplayerPageOpen = false;
		};

		/** The main menu: server browser, demo browser, mod manager, footer. */
		class MainScreenMainMenu : public ui::UIElement {
			MainScreenUI* ui;         // weak
			MainScreenHelper* helper; // weak

			ui::Field* addressField;

			ui::Button* protocol3Button;
			ui::Button* protocol4Button;

			ui::Button* filterProtocol3Button;
			ui::Button* filterProtocol4Button;
			ui::Button* filterFullButton;
			ui::Button* filterExtensionsButton;
			ui::Field* filterField;
			ui::Button* filterListButton;
			/** URL of the only server list to show; empty shows all of them. */
			std::string filterListUrl;

			ui::ListView* serverList;
			MainScreenServerListLoadingView* loadingView;
			MainScreenServerListErrorView* errorView;
			bool loading = false, loaded = false;

			Handle<ServerListModel> currentServerListModel;
			int serverListUpdateTimer = 5;
			std::string selectedMapName;

			TabPanel* serverPanel;

			// Demo tab state
			TabPanel* demoPanel;
			ui::ListView* demoList;
			Handle<DemoListModel> currentDemoListModel;
			std::string selectedDemoPath;
			ui::Field* demoNameField;
			ui::Button* demoPlayButton;
			ui::Button* demoDeleteButton;
			ui::Button* demoRenameButton;

			// Demo list column widths (pixels)
			float demoDateColWidth;
			float demoModeColWidth;
			float demoMapColWidth;
			float demoSizeColWidth;
			float demoContentsWidth;

			// Editor tab state (self-contained file-browser panel)
			EditorBrowserPanel* editorPanel;

			// Mods tab state
			Handle<ModsScreenHelper> modsHelper;
			Handle<ModPreviewCache> modPreviews; // the mod list's pictures
			TabPanel* modsPanel;
			ui::ListView* modsList;
			ui::Button* modsDownloadButton;
			ui::Button* modsApplyButton;
			ui::Button* modsResetButton;
			ui::Label* modsStatusLabel;
			ModsProgressBar* modsProgressBar;
			bool modsDownloading = false;
			bool modsDirty = false; // enabled set changed this session; restart to apply
			// The official mods check's state last shown in the status line (an
			// OfficialModsChecker::State), so the line follows it when it changes.
			int modsCheckState = -1;
			float modsCheckColWidth;
			float modsOrderColWidth;
			float modsTagColWidth;
			float modsNameColWidth;
			float modsAuthorColWidth;
			float modsOfficialColWidth;
			float modsSizeColWidth;

			// The home screen: a column of big buttons (Training, Multiplayer, ...),
			// shown first. The tabs and their footer are what Multiplayer, Demos and
			// Mods open, with Back to here.
			TabPanel* homePanel = nullptr;
			ui::Button* trainingButton = nullptr; // says so while the course starts
			bool trainingStarting = false;
			// The first-time VR setup (VRSetupScreen), checked once
			bool vrSetupChecked = false;
			ui::UIElement* vrSetup = nullptr; // weak: a child
			/** When the course's server was started, to give up on it in time. */
			double trainingStartedAt = 0.0;
			/** Starts the tutorial, or an aim scenario. */
			void StartTraining(const TrainingHost::Launch& launch = TrainingHost::Launch());

			// Singleplayer's page: the game modes in a list, and the chosen one's
			// own variants (an aim scenario, say), settings and Play beside it.
			// Each mode is described once here; the page is drawn from that, so a
			// new mode is a new entry, not new layout.
			struct SingleplayerSetting {
				std::string label;
				std::vector<std::string> options;
				std::function<int()> get;
				std::function<void(int)> set;
				std::vector<ui::Button*> buttons; // built
			};
			struct SingleplayerMode {
				std::string key; // kept in cg_singleplayerMode
				std::string name;
				std::string description;
				// Variants to pick one of, as "Name: what it is"; may be none
				std::vector<std::string> variants;
				std::vector<std::string> variantNames; // short, for the summary
				std::function<int()> getVariant;
				std::function<void(int)> setVariant;
				std::vector<SingleplayerSetting> settings;
				/** Right-hand text on its card in the list ("done", a count...). */
				std::function<std::string()> status;
				/** A variant's best as shown beside it, or "". */
				std::function<std::string(int)> variantBest;
				/** The line under the description (bests and such). */
				std::function<std::string()> detail;
				std::function<void()> play;
				// Built
				ui::Button* card = nullptr;
				TabPanel* page = nullptr;
				ui::Label* detailLabel = nullptr;
				std::vector<ui::Button*> variantButtons;
			};
			std::vector<SingleplayerMode> spModes;
			std::map<std::string, float> spBests; // read each time the page shows
			int spSelected = 0;
			// Narrow (or a small panel): the list and a mode's page are shown one
			// at a time, the page with its own Back.
			bool spNarrow = false;
			bool spPageOpen = false; // narrow only: a mode's page is up
			TabPanel* trainingPanel = nullptr;
			TabPanel* spListPane = nullptr;
			TabPanel* spDetailPane = nullptr;
			ui::Button* spPlayButton = nullptr;
			ui::Label* spSummary = nullptr;
			void BuildSingleplayerModes();
			void SelectSingleplayerMode(int index);
			void PlaySingleplayerMode();
			void ShowTraining();
			void RefreshTrainingPanel();
			void StartAimScenario(int scenario);
			void StartTowerDefense();
			/** Joins the course once its server listens, or says why it did not. */
			void PollTraining();
			ui::SimpleTabStrip* tabStrip = nullptr;
			std::vector<ui::UIElement*> tabFooter; // weak; shown with the tabs
			void ShowHome();
			void ShowTab(int index);

			void OnTabChanged(ui::UIElement& sender);

		public:
			/** Re-reads the listing of the tab currently on screen. */
			void RefreshVisibleTab();

		private:
			int EnabledIndex(const std::vector<std::string>& enabled, const std::string& name);
			void UpdateModsStatus();
			void OnDownloadModsPressed(ui::UIElement& sender);
			// Switches to the Mods tab and offers to download the official mods.
			void OfferOfficialModsDownload();
			void OnDownloadConfirmed(ui::UIElement& sender);
			void OnApplyModsPressed(ui::UIElement& sender);
			void OnResetModsPressed(ui::UIElement& sender);
			void OnResetConfirmed(ui::UIElement& sender);
			void OnModToggle(const std::string& modName);
			void CheckModsRefresh();

			void DemoListItemActivated(const std::string& filename);
			void DemoListItemDoubleClicked(const std::string& filename);
			void PlaySelectedDemo();
			void OnPlayDemoPressed(ui::UIElement& sender);
			void OnDeleteDemoPressed(ui::UIElement& sender);
			void OnDeleteConfirmClosed(ui::UIElement& sender);
			void OnRenameDemoPressed(ui::UIElement& sender);
			void OnRenameScreenClosed(ui::UIElement& sender);

			void ServerListItemActivated(MainScreenServerItem& item);
			void ServerListItemDoubleClicked(MainScreenServerItem& item);
			void ServerListItemRightClicked(MainScreenServerItem& item);

			void SortServerList(int keyId);
			void UpdateServerList(bool refresh = true);
			void ShowServerListFilter();
			void UpdateServerListFilterCaption();
			void CheckServerList();

			void OnAddressChanged(ui::UIElement& sender);
			void SetProtocolVersion(int ver);

			void ConfirmQuit();
			void OnQuitPressed(ui::UIElement& sender);
			void OnCreditsPressed(ui::UIElement& sender);
			void OnSetupPressed(ui::UIElement& sender);
			void OpenLink(const std::string& url);
			void Connect();

		public:
			MainScreenMainMenu(MainScreenUI* ui);

			void LoadServerList();
			void LoadDemoList();
			void LoadModList();

			MainScreenMainMenuState GetState();
			void SetState(const MainScreenMainMenuState& state);

			void HotKey(const std::string& key) override;
			void Render() override;
		};
	} // namespace gui
} // namespace spades
