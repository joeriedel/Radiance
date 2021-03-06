/*! \file Game.cpp
	\copyright Copyright (c) 2012 Sunside Inc., All Rights Reserved.
	\copyright See Radiance/LICENSE for licensing terms.
	\author Joe Riedel
	\ingroup world
*/

#include RADPCH
#include "../App.h"
#include "../Engine.h"
#include "Game.h"
#include "GameCVars.h"
#include "GSLoadMap.h"
#include "GSPlay.h"
#include "../World/World.h"
#include "../Assets/MapAsset.h"
#include "../Sound/Sound.h"
#include <Runtime/Time.h>
#include <Runtime/Math.h>
#if defined(RAD_OPT_GL)
#include "../Renderer/GL/GLState.h"
#endif

#if defined(RAD_OPT_PC_TOOLS)
#include "../Tools/Editor/EditorMainWindow.h"
#endif

#include <algorithm>

#undef min
#undef max

enum {
	kPinchGestureDelayMs = 110
};


Game::Game(GameUIMode uiMode) : 
m_slot(0), 
m_pinch(0), 
m_pinchDelay(0), 
m_cloudStorage(false),
m_quit(false),
m_uiMode(uiMode)
#if defined(RAD_OPT_PC_TOOLS)
, m_toolsCallback(0), m_progressIndicatorParent(0)
#endif
{
	m_vp[0] = m_vp[1] = m_vp[2] = m_vp[3] = 0;
	m_session = Persistence::Load(0);
	m_saveGame = Persistence::Load(0);

	m_cvarZone.Open(0);
	m_cvars = new GameCVars(*this, m_cvarZone);
}

Game::~Game() {
#if !defined(RAD_OPT_SHIP)
	m_dbgServer.reset();
#endif
	m_cvarZone.Close();
	delete m_cvars;
}

bool Game::LoadEntry() {
	return true;
}

void Game::SetViewport(int x, int y, int w, int h) {
	m_vp[0] = x;
	m_vp[1] = y;
	m_vp[2] = w;
	m_vp[3] = h;
}

void Game::Viewport(int &x, int &y, int &w, int &h) {
	x = m_vp[0];
	y = m_vp[1];
	w = m_vp[2];
	h = m_vp[3];
}

#if defined(RAD_OPT_PC_TOOLS)
void Game::Tick(float dt, IToolsCallbacks *cb)
#else
void Game::Tick(float dt)
#endif
{
#if defined(RAD_OPT_PC_TOOLS)
	if (m_inputState.kb.keys[kKeyCode_Up].state)
		dt *= 4.f;
	if (m_inputState.kb.keys[kKeyCode_Down].state)
		dt /= 8.f;
	
	m_toolsCallback = cb;
#endif

	if (m_slot && m_slot->active) {
		m_gameNetworkEventQueue.Dispatch(*m_slot->active->world.get());
		m_storeEventQueue.Dispatch(*m_slot->active->world.get());
	}

	OnTick(dt);
	DoTickable(dt);
	FlushInput();
}

void Game::LoadMap(int id, int slot, world::UnloadDisposition ud, bool play, bool loadScreen) {
	if (ud == world::kUD_All) {
		App::Get()->engine->sys->r->UnbindStates();
		m_maps.clear();
	}

	MapSlot *s = &m_maps[slot];

	if (ud == world::kUD_Slot) {
		App::Get()->engine->sys->r->UnbindStates();
		s->queue.clear();
		s->active.reset();
		App::Get()->engine->sys->r->BindFramebuffer();
	} else {
		if (s->active) {
			s->queue.push_back(s->active);
			s->active->world->NotifyBackground();
			s->active.reset();
		}
	}

	Switch(slot);
	RAD_ASSERT(m_slot == s);

	Tickable::Ref load = GSLoadMap::New(id, slot, play, loadScreen);
#if defined(RAD_OPT_PC_TOOLS)
	static_cast<GSLoadMap*>(load.get())->EnableProgressIndicator(m_progressIndicatorParent);
#endif
	Push(load);
}

bool Game::LoadMap(const char *name, int slot, world::UnloadDisposition ud, bool play, bool loadScreen) {
	int id = App::Get()->engine->sys->packages->ResolveId(name);
	if (id >= 0) {
		COut(C_Debug) << "Loading Map \"" << name << "\"" << std::endl;
		LoadMap(id, slot, ud, play, loadScreen);
		return true;
	}
	
	COut(C_Debug) << "Failed to resolve id for \"" << name << "\"" << std::endl;
	return false;
}

bool Game::LoadMapSeq(int id, int slot, world::UnloadDisposition ud, bool play) {
	if (ud == world::kUD_All) {
		App::Get()->engine->sys->r->UnbindStates();
		m_maps.clear();
	}

	MapSlot *s = &m_maps[slot];
	if (ud == world::kUD_Slot) {
		App::Get()->engine->sys->r->UnbindStates();
		s->queue.clear();
		s->active.reset();
		App::Get()->engine->sys->r->BindFramebuffer();
	} else if (s->active) {
		s->queue.push_back(s->active);
		s->active->world->NotifyBackground();
		s->active.reset();
	}

	Switch(slot);
	RAD_ASSERT(m_slot == s);

	pkg::Asset::Ref map = App::Get()->engine->sys->packages->Asset(id, pkg::Z_Engine);
	if (!map) {
		// TODO: handle failed loading
		COut(C_ErrMsgBox) << "Error loading map!" << std::endl;
		return false;
	}

	asset::MapAsset *mapAsset = asset::MapAsset::Cast(map);
	if (!mapAsset) {
		COut(C_ErrMsgBox) << "Error loading map!" << std::endl;
		return false;
	}
	
#if !defined(RAD_OPT_SHIP)
	if (!m_dbgServer) {
		m_dbgServer = tools::DebugConsoleServer::Start(map->path, &m_cvarZone);
	} else {
		m_dbgServer->SetDescription(map->path);
	}
#endif

	mapAsset->SetGame(*this, slot);

	int r = pkg::SR_Pending;

#if defined(RAD_OPT_PC_TOOLS)
	int kLoadFlags = (tools::editor::MainWindow::Get()->lowQualityPreview) ? pkg::P_FastCook : 0;
#endif

	while (r == pkg::SR_Pending) {
		xtime::TimeVal start = xtime::ReadMilliseconds();
		xtime::TimeSlice slice(100);

		r = map->Process(
			slice,
			pkg::P_Load|pkg::P_FastPath
#if defined(RAD_OPT_PC_TOOLS)
			| kLoadFlags
#endif
		);

		xtime::TimeVal elapsed = xtime::ReadMilliseconds() - start;

		if (elapsed > 0) { // tick music
			world::World::Ref world = mapAsset->world;
			if (world)
				world->sound->Tick(elapsed/1000.f, false);

#if !defined(RAD_OPT_SHIP)
			if (m_dbgServer)
				m_dbgServer->ProcessClients();
#endif
		}
	}

	if (r != pkg::SR_Success)
		return false;

	Game::Map::Ref gmap(new (ZWorld) Game::Map());
	gmap->world = mapAsset->world;
	gmap->asset = map;
	gmap->id = id;
	m_slot->active = gmap;
	FlushInput(true);

	if (play)
		Play();

	App::DumpMemStats(C_Debug);
	App::Get()->throttleFramerate = cvars->r_throttle.value; // throttle framerate if supported.
	return true;
}

bool Game::LoadMapSeq(const char *name, int slot, world::UnloadDisposition ud, bool play) {
	int id = App::Get()->engine->sys->packages->ResolveId(name);
	if (id >= 0) {
		COut(C_Debug) << "Loading Map \"" << name << "\"" << std::endl;
		return LoadMapSeq(id, slot, ud, play);
	}
	
	COut(C_Debug) << "Failed to resolve id for \"" << name << "\"" << std::endl;
	return false;
}

void Game::Return() {
	if (!m_slot)
		return;

	App::Get()->engine->sys->r->UnbindStates();
	m_slot->active.reset();
	if (!m_slot->queue.empty()) {
		m_slot->active = m_slot->queue.back();
		m_slot->queue.pop_back();
		if (m_slot->active)
			m_slot->active->world->NotifyResume();
	}
}

void Game::Play() {
	FlushInput(true);
	Push(GSPlay::New());
}

void Game::Switch(int slot) {
	FlushInput(true);
	MapSlot *next = &m_maps[slot];
	if (m_slot && next != m_slot && m_slot->active)
		m_slot->active->world->NotifyBackground();
	if (next->active)
		next->active->world->NotifyResume();
	m_slot = next;
}

void Game::Unload(int slot) {
	App::Get()->engine->sys->r->UnbindStates();

	MapSlot *s = &m_maps[slot];
	if (s == m_slot)
		m_slot = 0;
	s->queue.clear();
	s->active.reset();
}

void Game::CreateSaveGame(const char *name) {
	RAD_ASSERT(name);

	// disconnect any cloud storage
	m_cloudFile.reset();
	m_cloudVersions.clear();

	m_saveGameName = name;

	if (m_cloudStorage && CloudStorage::Enabled()) {
		m_cloudVersions = CloudStorage::Resolve(name);
		if (!m_cloudVersions.empty())
			m_cloudFile = m_cloudVersions[0];
	}

	m_saveGame = Persistence::New(name);
}

void Game::LoadSavedGame(const char *name) {
	RAD_ASSERT(name);

	// disconnect any cloud storage
	m_cloudFile.reset();
	m_cloudVersions.clear();

	m_saveGameName = name;

	if (m_cloudStorage && CloudStorage::Enabled()) {
		m_cloudVersions = CloudStorage::Resolve(name);
		if (!m_cloudVersions.empty())
			m_cloudFile = m_cloudVersions[0];
	}

	if (m_cloudFile) {
		stream::InputStream is(m_cloudFile->ib);
		m_saveGame = Persistence::Load(is);
		m_saveGame->Save(name);
	} else {
		m_saveGame = Persistence::Load(name);
	}
}

void Game::SaveGame() {
	if (!m_cloudStorage) {
		m_cloudFile.reset(); // disconnect.
		m_cloudVersions.clear();
	}

	if (m_cloudFile) {
		stream::OutputStream os(m_cloudFile->ob);
		m_saveGame->Save(os);
	}
	
	m_saveGame->Save();
}

void Game::LoadSavedGameConflict(int num) {
	if (num < (int)m_cloudVersions.size()) {
		CloudFile::Ref file = m_cloudVersions[num];
		stream::InputStream is(file->ib);
		m_saveGame = Persistence::Load(is);
	}
}

void Game::ResolveSavedGameConflict(int chosen) {
	if (chosen < (int)m_cloudVersions.size()) {
		CloudStorage::ResolveConflict(m_cloudVersions[chosen]);
		m_cloudVersions.clear();
	}
}

void Game::NotifySaveState() {
	FlushInput(true);
	if (m_slot && m_slot->active)
		m_slot->active->world->SaveApplicationState();
}

void Game::NotifyRestoreState() {
	FlushInput(true);
	if (m_slot && m_slot->active)
		m_slot->active->world->RestoreApplicationState();
}

int Game::OnWorldInit(world::World &world) {
	return pkg::SR_Success;
}

void Game::PostInputEvent(const InputEvent &e) {
#if defined(RAD_OPT_PC) && !defined(RAD_TARGET_GOLDEN)
	InputEvent x(e);

	// Only translate LEFT mouse button
	if ((x.type == InputEvent::T_MouseUp || x.type == InputEvent::T_MouseDown || x.type == InputEvent::T_MouseMove) && (x.data[2]&kMouseButton_Left)) {
		x.touch = (void*)1; // set touch
		m_inputEvents.push_back(x);
	}	
	else
	{
#endif

	m_inputEvents.push_back(e);

#if defined(RAD_OPT_PC) && !defined(RAD_TARGET_GOLDEN)
	}
#endif
}

void Game::FlushInput(bool reset) {
	m_inputEvents.clear(); // eat any unprocessed input
	m_inputState.ms.delta[0] = 0;
	m_inputState.ms.delta[1] = 0;
	m_inputState.ms.dwheel = 0;

	if (reset) {
		m_delayedEvents.clear();
		m_pinch = 0;
		m_pinchTouches.clear();
		m_inputState.touches.clear();
		m_inputState.ms.buttons = 0;
		m_inputState.ms.wheel = 0;
		m_doubleTap.type = InputEvent::T_Invalid;

		for (int i = 0; i < kKeyCode_Max; ++i) {
			m_inputState.kb.keys[i].state = false;
			m_inputState.kb.keys[i].impulse = false;
		}
	} else {
		xtime::TimeVal now = xtime::ReadMilliseconds();

		for (int i = 0; i < kKeyCode_Max; ++i) {
			if (m_inputState.kb.keys[i].state &&
				m_inputState.kb.keys[i].impulse &&
				(now-m_inputState.kb.keys[i].time) > 200) {
				m_inputState.kb.keys[i].impulse = false;
			}
		}
	}
}

InputEvent *Game::DelayedEvent(const InputEvent &e) {
	if (e.touch) {
		for(InputEventList::iterator it = m_delayedEvents.begin(); it != m_delayedEvents.end(); ++it) {
			if ((*it).touch == e.touch)
				return &(*it);
		}
	}

	return 0;
}

InputEvent *Game::CreateDelayedEvent(const InputEvent &e) {
	m_delayedEvents.push_back(e);
	return &m_delayedEvents.back();
}

void Game::RemoveDelayedEvent(void *touch) {
	for(InputEventList::iterator it = m_delayedEvents.begin(); it != m_delayedEvents.end(); ++it) {
		if ((*it).touch == touch) {
			m_delayedEvents.erase(it);
			break;
		}
	}
}

void Game::ProcessInput() {
	InputGesture g;

	int enabledGestures = 0;
	if (m_slot && m_slot->active)
		enabledGestures = m_slot->active->world->enabledGestures;

	xtime::TimeVal time = xtime::ReadMilliseconds();

	// moved delayed events into head of event queue based on time, and preserve order

	for (InputEventList::reverse_iterator it = m_delayedEvents.rbegin(); it != m_delayedEvents.rend();) {
		const InputEvent &e = *it;

		if (time >= e.delay) {
			m_inputEvents.push_front(e);
			InputEventList::iterator x = m_delayedEvents.erase(--it.base()); // reverse_iterator(i) = (i - 1), base() == i
			it = InputEventList::reverse_iterator(x);
		} else {
			++it;
		}
	}
	
	while (!m_inputEvents.empty()) {
		InputEvent e = m_inputEvents.front();
		InputEvent *delayed = 0;
		bool gesture = true;

		TouchState *touch = UpdateState(e, m_inputState);

		// NOTE: touch may be NULL if event generated by mouse or keyboard.

		if (touch && (e.IsTouch()
#if !defined(RAD_TARGET_GOLDEN)
			|| (e.IsMouse() && (e.data[2] == kMouseButton_Left))
#endif
			)) {
			delayed = DelayedEvent(e);
			if (delayed) {
				if (e.IsTouchEnd(0)
#if !defined(RAD_TARGET_GOLDEN)
					|| (e.type == InputEvent::T_MouseUp)
#endif
					) {  
					// touch ended before delay time, run touch+end through input system

					if ((m_pinchTouches.size() < 2) && (!touch || (touch->gid == -1 || touch->gid == IG_Null))) { 
						// no gestures initiated from this event
						if (OnInputEvent(*delayed, touch, m_inputState)) {
							if (touch)
								touch->gid = IG_Null; // input event accepted this cannot generate a gesture
						}
					}

					if (m_inputEvents.empty())
						break; // flushed

					if (enabledGestures && touch && touch->gid != IG_Null && !delayed->gesture) {
						e.gesture = true;

						if (GestureInput(*delayed, m_inputState, g, *touch, enabledGestures)) {
							OnGesture(g, *touch, m_inputState);
						}
					}

					if (m_inputEvents.empty())
						break; // flushed

					if ((m_pinchTouches.size() < 2) && (!touch || (touch->gid == -1 || touch->gid == IG_Null))) { 
						// no gestures initiated from this event
						if (OnInputEvent(e, touch, m_inputState)) {
							if (touch)
								touch->gid = IG_Null; // input event accepted this cannot generate a gesture
						}
					}

					if (m_inputEvents.empty())
						break; // flushed

					if (enabledGestures && touch && touch->gid != IG_Null && !e.gesture) {
						e.gesture = true;

						if (GestureInput(e, m_inputState, g, *touch, enabledGestures)) {
							OnGesture(g, *touch, m_inputState);
						}

						touch->gesture = true;
					}

					if (m_inputEvents.empty())
						break; // flushed

					// touch ended before delay time
					RemoveDelayedEvent(e.touch);
				} else {
					e.delay = delayed->delay;
					e.gesture = delayed->gesture;
					*delayed = e;
					delayed->type = InputEvent::T_TouchBegin;
				}

				if (!m_inputEvents.empty())
					m_inputEvents.pop_front();
				continue; // process later
			}
		}

		// delay touch for pinch recognition?
		if ((enabledGestures&IG_Pinch) && touch && touch->gid != IG_Null && (e.IsTouch() 
#if !defined(RAD_TARGET_GOLDEN)
			|| (e.IsMouse() && (e.data[2] == kMouseButton_Left))
#endif
			) && !e.gesture) {
			// do we need to delay this event to see if it may generate a pinch?
			if (!e.delay && ((e.type == InputEvent::T_TouchBegin)
#if !defined(RAD_TARGET_GOLDEN)
				|| (e.type == InputEvent::T_MouseDown)
#endif
				) && (m_pinchTouches.size() < 2)) {
				e.gesture = true;

				if (m_pinchTouches.empty()) {
					delayed = CreateDelayedEvent(e);
					delayed->delay = xtime::ReadMilliseconds() + kPinchGestureDelayMs;
				}
				
				// see if this starts a pinch action
				gesture = false;

				if (GestureInput(e, m_inputState, g, *touch, enabledGestures)) {
					OnGesture(g, *touch, m_inputState);

					if (touch->gid == IG_Pinch) { 
						// remove all delayed touches related to this event.
						RAD_VERIFY(m_pinchTouches.size() == 2);
						TouchSet::iterator it = m_pinchTouches.begin();
						RemoveDelayedEvent(*it);
						++it;
						RemoveDelayedEvent(*it);
					}
				}

				touch->gesture = true;
			}
		}

		if (!delayed && (m_pinchTouches.size() < 2) && (!touch || (touch->gid == -1 || touch->gid == IG_Null))) { 
			// no gestures initiated from this event
			if (OnInputEvent(e, touch, m_inputState)) {
				if (touch)
					touch->gid = IG_Null; // input event accepted this cannot generate a gesture
			}
		}

		if (m_inputEvents.empty())
			break; // flushed

		if (gesture && enabledGestures && touch && touch->gid != IG_Null && (e.IsTouch() 
#if !defined(RAD_TARGET_GOLDEN)
			|| (e.IsMouse() && (e.data[2] == kMouseButton_Left))
#endif
			) && !e.gesture) {
			e.gesture = true;

			if (GestureInput(e, m_inputState, g, *touch, enabledGestures)) {
				OnGesture(g, *touch, m_inputState);
			}

			touch->gesture = true;
		}

		if (!m_inputEvents.empty()) // may have flushed
			m_inputEvents.pop_front();
	}

	// Clear I_TouchEnd touches.

	for (InputTouchMap::iterator it = m_inputState.touches.begin(); it != m_inputState.touches.end();) {
		const TouchState &s = it->second;
		if (s.e.IsTouchEnd(0)
#if !defined(RAD_TARGET_GOLDEN)
			|| ((s.e.type == InputEvent::T_MouseUp) && (s.e.data[2] == kMouseButton_Left))
#endif
			) {
			InputTouchMap::iterator next = it; ++next;
			m_inputState.touches.erase(it);
			it = next;
		} else {
			++it;
		}
	}
}

TouchState *Game::UpdateState(const InputEvent &e, InputState &is) {
	TouchState *touchState = 0;

	// maintain input state.
	switch (e.type) {
	case InputEvent::T_KeyDown:
		is.kb.keys[e.data[0]].state = true;
		is.kb.keys[e.data[0]].impulse = true;
		is.kb.keys[e.data[0]].time = e.time;
		break;
	case InputEvent::T_KeyUp:
		is.kb.keys[e.data[0]].state = false;
		is.kb.keys[e.data[0]].impulse = false;
		is.kb.keys[e.data[0]].time = e.time;
		break;
	case InputEvent::T_MouseDown:
		is.ms.buttons |= e.data[2];
		is.ms.time = e.time;
		break;
	case InputEvent::T_MouseUp:
		is.ms.buttons &= ~e.data[2];
		break;
	case InputEvent::T_MouseWheel:
		is.ms.wheel += e.data[2];
		is.ms.dwheel = e.data[2];
		break;
	case InputEvent::T_MouseMove:
		is.ms.delta[0] = e.data[0]-is.ms.wpos[0];
		is.ms.delta[1] = e.data[1]-is.ms.wpos[1];
		is.ms.dpos[0] += is.ms.delta[0];
		is.ms.dpos[1] += is.ms.delta[1];
		is.ms.wpos[0] = e.data[0];
		is.ms.wpos[1] = e.data[1];	

		// clamp delta-pos to viewport.
		is.ms.dpos[0] = std::min(is.ms.dpos[0], m_vp[0]+m_vp[2]);
		is.ms.dpos[0] = std::max(is.ms.dpos[0], m_vp[0]);
		is.ms.dpos[1] = std::min(is.ms.dpos[1], m_vp[1]+m_vp[3]);
		is.ms.dpos[1] = std::max(is.ms.dpos[1], m_vp[1]);
		break;
	default:
		break;
	}

	switch (e.type) {
#if !defined(RAD_TARGET_GOLDEN)
	case InputEvent::T_MouseDown: // touch emulation on PC
	case InputEvent::T_MouseUp:
	case InputEvent::T_MouseMove:
		if (e.data[2] != kMouseButton_Left)
			break;
#endif
	case InputEvent::T_TouchBegin:
	case InputEvent::T_TouchMoved:
	case InputEvent::T_TouchStationary:
	case InputEvent::T_TouchEnd:
	case InputEvent::T_TouchCancelled:
		{
			TouchState s;
			TouchState *local = &s;

			if ((e.type == InputEvent::T_TouchBegin) ||
				(e.type == InputEvent::T_MouseDown)) {
				local->startTime = e.time; // start time
				local->clockTime = xtime::ReadMilliseconds();
				local->begin = true;
				local->moves.reserve(32);
			} else {
				InputTouchMap::iterator it = is.touches.find(e.touch);
				if (it != is.touches.end()) {
					touchState = &it->second;
					local = touchState;
				}
			}

			local->e = e;
			local->mins[0] = std::min(local->mins[0], e.data[0]);
			local->maxs[0] = std::max(local->maxs[0], e.data[0]);
			local->mins[1] = std::min(local->mins[1], e.data[1]);
			local->maxs[1] = std::max(local->maxs[1], e.data[1]);

			InputPoint point;
			point[0] = e.data[0];
			point[1] = e.data[1];
			local->moves.push_back(point);

			if (!touchState) {
				std::pair<InputTouchMap::iterator, bool> x = is.touches.insert(
					InputTouchMap::value_type(e.touch, TouchState())
				); // insert empty state and SwapCopy() because it's faster.

				touchState = &x.first->second;
				touchState->SwapCopy(*local);
			}
		}
		break;
	default:
		break;
	}

	return touchState;
}

void Game::Push(const Tickable::Ref &state) {
	m_tickable.Push(state);
}

void Game::Pop() {
	m_tickable.Pop();
}

void Game::OnTick(float dt) {
}

void Game::DoTickable(float dt) {
	m_tickable.Tick(*this, dt, xtime::TimeSlice::Infinite, 0);
}

bool Game::OnInputEvent(const InputEvent &e, const TouchState *touch, const InputState &is) {
	bool r = false;
	if (m_slot && m_slot->active)
		r = m_slot->active->world->HandleInputEvent(e, touch, is);
	return r;
}

bool Game::OnGesture(const InputGesture &g, const TouchState &touch, const InputState &is) {
	bool r = false;
	if (m_slot && m_slot->active)
		r = m_slot->active->world->HandleInputGesture(g, touch, is);
	return r;
}

bool Game::CreateGameNetwork() {
	if (!m_gameNetwork)
		m_gameNetwork = gn::GameNetwork::Create(&m_gameNetworkEventQueue);
	return m_gameNetwork;
}

bool Game::CreateStore() {
	if (!m_store)
		m_store = iap::Store::Create(&m_storeEventQueue);
	return m_store;
}

void Game::PlayFullscreenMovie(const char *path) {
#if defined(RAD_OPT_PC_TOOLS)
	m_toolsCallback->PlayFullscreenMovie(path);
#else
	App::Get()->PlayFullscreenMovie(path);
#endif
}

void Game::MovieFinished() {
	if (m_slot && m_slot->active)
		m_slot->active->world->MovieFinished();
}

void Game::EnterPlainTextDialog(const char *title, const char *message) {
#if defined(RAD_OPT_PC_TOOLS)
	m_toolsCallback->EnterPlainTextDialog(title, message);
#else
	App::Get()->EnterPlainTextDialog(title, message);
#endif
}

void Game::PlainTextDialogResult(bool cancel, const char *text) {
	if (m_slot && m_slot->active)
		m_slot->active->world->PlainTextDialogResult(cancel, text);
}

#if defined(RAD_OPT_PC_TOOLS)
void Game::EnableProgressIndicator(QWidget *parent) {
	m_progressIndicatorParent = parent;
}
#endif
