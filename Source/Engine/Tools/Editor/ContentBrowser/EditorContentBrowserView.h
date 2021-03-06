// EditorContentBrowserView.h
// Copyright (c) 2010 Sunside Inc., All Rights Reserved
// Author: Joe Riedel
// See Radiance/LICENSE for licensing terms.

#pragma once

#include "../EditorTypes.h"
#include "EditorContentAssetThumbDimensionCache.h"
#include "EditorContentBrowserDef.h"
#include "../../../Assets/AssetTypes.h"
#include "../../../Game/GameDef.h"
#include <QtGui/QWidget>
#include <QtGui/QResizeEvent>
#include <QtGui/QFont>
#include <QtGui/QMenu>
#include <Runtime/Container/ZoneSet.h>
#include <Runtime/Container/ZoneMap.h>
#include <Runtime/Container/ZoneMap.h>
#include <Runtime/Container/ZoneVector.h>
#include <Runtime/Thread/Locks.h>
#include "../../../Packages/Packages.h"
#include "../../../Renderer/GL/GLTexture.h"
#include <utility>
#include <Runtime/PushPack.h>

class QScrollBar;
class QComboBox;
class QMouseEvent;
class QWheelEvent;
class QPushButton;
class QKeyEvent;

namespace tools {
namespace editor {

///////////////////////////////////////////////////////////////////////////////

class GLWidget;
class ContentBrowserViewThread;
class ContentBrowserWindow;
class ContentBrowserView;
class PopupMenu;
class SearchLineWidget;

///////////////////////////////////////////////////////////////////////////////

class ContentAssetThumb : public QObject
{
	Q_OBJECT
public:
	typedef boost::shared_ptr<ContentAssetThumb> Ref;
	typedef zone_map<asset::Type, Ref, ZEditorT>::type Map;
	ContentAssetThumb(ContentBrowserView &view);
	virtual ~ContentAssetThumb() {}
	virtual void Dimensions(const pkg::Package::Entry::Ref &entry, int &w, int &h);
	virtual bool Render(const pkg::Package::Entry::Ref &entry, int x, int y, int w, int h);
	virtual void OpenEditor(const pkg::Package::Entry::Ref &entry, bool editable, bool modal);
	virtual void RightClick(const pkg::Package::Entry::Ref &entry, QMouseEvent *e, bool editable, bool modal);
	virtual void Begin();
	virtual void End();
	virtual void NotifyAddRemovePackages();
	virtual void NotifyAddRemoveContent(const pkg::IdVec &added, const pkg::IdVec &removed);
	virtual void NotifyContentChanged(const ContentChange::Vec &changed);
	virtual void Tick(float dt, const xtime::TimeSlice &time);
	ContentBrowserView &View() const;
	void ThumbChanged();
protected:
	RAD_DECLARE_READONLY_PROPERTY(ContentAssetThumb, clickedItem, pkg::Package::Entry::Ref);
	void Register(Ref &self, asset::Type type);
	PopupMenu *CreateMenu(bool mutableMenu);
	PopupMenu *Menu(bool mutableMenu) { return m_menu[mutableMenu ? 0 : 1]; }
	void ItemMode(bool &editable, bool &modal) {
		editable = m_editable;
		modal = m_modal;
	}
private:
	RAD_DECLARE_GET(clickedItem, pkg::Package::Entry::Ref) { return m_item; }
	pkg::Package::Entry::Ref m_item;
	bool m_editable, m_modal;
	PopupMenu *m_menu[2];
	ContentBrowserView *m_view;
};

///////////////////////////////////////////////////////////////////////////////

class RADENG_CLASS ContentBrowserView : public QWidget
{
	Q_OBJECT
public:
	typedef zone_set<ContentBrowserView*, ZEditorT>::type ViewSet;

	///////////////////////////////////////////////////////////////////////////////

	enum Sort
	{
		SortName,
		SortSize,
		SortSizeName
	};

	enum SelMode
	{
		SelNone,
		SelSingle,
		SelMulti
	};

	enum Icon
	{
		I_OneDisk,
		I_TwoDisks,
		I_Error,
		I_Max,
		I_AnimRange=2
	};

	struct Filter
	{
		struct Package
		{
			typedef zone_vector<Package, ZEditorT>::type Vec;
			Package() {}
			Package(const Package &_pkg) : pkg(_pkg.pkg), types(_pkg.types) {}
			explicit Package(const pkg::Package::Ref &_pkg)
				: pkg(_pkg)
			{
				types.set(); // types.
			}
			pkg::Package::Ref pkg;
			asset::TypeBits types;
		};

		Filter() : sort(SortSizeName) { types.set(); }
		Filter(const Filter &s) :
			sort(s.sort), pkgs(s.pkgs), assets(s.assets), types(s.types) {}
		explicit Filter(
			Sort _sort
		) : sort(_sort) { types.set(); }
		Filter(
			Sort _sort,
			const Package::Vec &_pkgs,
			const pkg::IdVec &_assets,
			const asset::TypeBits &_types
		) : sort(_sort), pkgs(_pkgs), assets(_assets), types(_types) {}

		int sort;
		Package::Vec pkgs;
		pkg::IdVec assets;
		asset::TypeBits types;
		String filter;

	private:

		friend class ContentBrowserView;
		pkg::Package::Entry::Ref Begin();
		pkg::Package::Entry::Ref Next();

		const pkg::PackageMap *mpkgs;
		Package::Vec::const_iterator vpkgIt;
		pkg::Package::Map::const_iterator mpkgIt;
		const pkg::StringIdMap *mids;
		pkg::StringIdMap::const_iterator midIt;
		pkg::IdVec::const_iterator aIt;
	};

	///////////////////////////////////////////////////////////////////////////////

	ContentBrowserView(
		bool vscroll = true,
		bool editable = true,
		bool modal = false,
		SelMode selMode = SelMulti,
		QWidget *parent = 0,
		Qt::WindowFlags f = 0
	);

	virtual ~ContentBrowserView();

	void RecalcLayout(bool redraw=false);
	void BuildAssetList();
	void Update();
	bool ScrollToSelection();
	bool ScrollToAsset(int id);
	void Select(int id, bool clear=true, bool redraw=true);
	void Select(const pkg::IdVec &ids, bool clear=true, bool redraw=true);
	ContentAssetThumb *ThumbForType(asset::Type type);
	r::GLTexture::Ref GetIcon(Icon i) const;
	std::pair<int, int> selectedResolution() const;

	static void LoadCache();
	static const ViewSet &Views() { return s_views; }
	static void Tick(float t);

	RAD_DECLARE_READONLY_PROPERTY(ContentBrowserView, filter, Filter*);
	RAD_DECLARE_PROPERTY(ContentBrowserView, selMode, SelMode, SelMode);
	RAD_DECLARE_READONLY_PROPERTY(ContentBrowserView, errorIcon, r::GLTexture::Ref);
	RAD_DECLARE_READONLY_PROPERTY(ContentBrowserView, uiMode, GameUIMode);

	typedef zone_set<int, ZEditorT>::type SelSet;

	RAD_DECLARE_READONLY_PROPERTY(ContentBrowserView, selection, const SelSet&);

signals:

	void OnSelectionChanged(
		const SelSet &sel,   // what was selected (if any) 
		const SelSet &unsel, // what was deselected
		const SelSet &total  // total selection
	);

	void OnCloneSelection(const SelSet &sel);
	void OnMoveSelection(const SelSet &sel);
	void OnItemDoubleClicked(int id, bool &openEditor);

public slots:

	void ClearSelection(bool event=true);
	void UpdateFilter(bool redraw=true);

private slots:
	void OnRenderGL(GLWidget&);
	void OnResizeGL(GLWidget&, int, int);
	void OnInitializeGL(GLWidget&);
	void OnScrollMove(int pos);
	void SortChanged(int index);
	void SizeChanged(int index);
	void ResolutionChanged(int index);
	void UIModeChanged(int index);
	void ZoomIn();
	void ZoomOut();
	void ZoomFull();
	void DeleteSelection();
	void CloneSelection();
	void MoveSelection();
	void OnWheelEvent(QWheelEvent *e);
	void OnMouseMoveEvent(QMouseEvent *e);
	void OnMousePressEvent(QMouseEvent *e);
	void OnMouseReleaseEvent(QMouseEvent *e);
	void OnMouseDoubleClickEvent(QMouseEvent *e);
	void OnKeyPressEvent(QKeyEvent *e);

private:

	friend class ContentAssetThumb;
	friend class ContentBrowserWindow;
	friend class ContentBrowserViewThread;

	static void NotifyAddRemovePackages();
	static void NotifyAddRemoveContent(const pkg::IdVec &added, const pkg::IdVec &removed);
	static void NotifyContentChanged(const ContentChange::Vec &changed);

	RAD_DECLARE_GET(filter, Filter*) { return &m_filter; }
	RAD_DECLARE_GET(selMode, SelMode) { return m_selMode; }
	RAD_DECLARE_SET(selMode, SelMode);
	RAD_DECLARE_GET(selection, const SelSet &) { return m_sel; }
	RAD_DECLARE_GET(errorIcon, r::GLTexture::Ref) { return GetIcon(I_Error); }
	RAD_DECLARE_GET(uiMode, GameUIMode) { return (GameUIMode)m_uiMode; }

	void _SortName();
	void _SortSize(bool sortName);

	void DoTick(float dt);
	void ThumbChanged();
	void RegisterThumb(const ContentAssetThumb::Ref &thumb, asset::Type type);
	void DoSelectionChange(
		const SelSet &sel,   // what was selected (if any) 
		const SelSet &unsel, // what was deselected
		const SelSet &total  // total selection
	);
	
	struct AssetInfo
	{
		int x, y, w, iw, ih, tw;
		int id;
	};

	typedef zone_vector<AssetInfo, ZEditorT>::type AssetInfoVec;
	
	struct Row
	{
		typedef zone_map<int, Row, ZEditorT>::type Map;
		int y[2];
		AssetInfoVec assets;
	};

	struct IdPos
	{
		typedef zone_map<int, IdPos, ZEditorT>::type Map;
		int id;
		int idx;
		Row::Map::iterator row;
	};

	void BuildRows();
	void ShowScroll(bool show);
	void DrawThumb(ContentAssetThumb *thumb, const pkg::Package::Entry::Ref &asset, int x, int y, int w, int h);
	void LoadIcons();
	int HitTestId(int x, int y, const Row** row=0, const AssetInfo **info=0) const;
	SelSet ItemRange(int start, int end) const;
	int TopLeft(const SelSet &ids, const Row** row=0, const AssetInfo **info=0) const;
	int BottomRight(const SelSet &ids, const Row** row=0, const AssetInfo **info=0) const;
	void KnockoutSelection();

	typedef boost::mutex Mutex;
	typedef boost::lock_guard<Mutex> Lock;
	typedef zone_vector<int, ZEditorT>::type IdVec;

	int m_y[2];
	int m_maxs[2];
	int m_hoverId;
	int m_step;
	int m_resolution;
	int m_uiMode;
	bool m_vscroll;
	bool m_editable;
	bool m_modal;
	bool m_loadedIcons;
	bool m_inTick;
	bool m_tickRedraw;
	SelMode m_selMode;
	QScrollBar *m_scroll;
	QComboBox *m_sort;
	QComboBox *m_sizes;
	QComboBox *m_resolutions;
	QComboBox *m_uiModes;
	QPushButton *m_delButton;
	QPushButton *m_cloneButton;
	QPushButton *m_moveButton;
	SearchLineWidget *m_searchLine;
	GLWidget *m_glw;
	Row::Map m_rows;
	IdPos::Map m_ids;
	QFont m_font;
	ContentAssetThumb::Map m_thumbs;
	IdVec m_assets;
	SelSet m_sel;
	r::GLTexture::Ref m_icons[I_Max];
	mutable Filter m_filter;

	static int AddView(ContentBrowserView *view);
	static void RemoveView(ContentBrowserView *view);
	static Mutex s_m;
	static ViewSet s_views;
};

} // editor
} // tools

#include <Runtime/PopPack.h>
