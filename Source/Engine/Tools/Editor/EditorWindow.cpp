// EditorWindow.cpp
// Copyright (c) 2010 Sunside Inc., All Rights Reserved
// Author: Joe Riedel
// See Radiance/LICENSE for licensing terms.

#include RADPCH
#include "EditorWindow.h"
#include "EditorMainWindow.h"
#include "ContentBrowser/EditorContentBrowserWindow.h"
#include "ContentBrowser/EditorContentBrowserView.h"
#include "EditorUtils.h"
#include <QtGui/QVBoxLayout>
#include <QtGui/QKeyEvent>
#include <QtGui/QPushButton>

namespace tools {
namespace editor {

EditorWindow::EditorWindow(
	WidgetStyle style,
	ButtonFlags buttons,
	bool deleteOnClose,
	bool enableTick,
	QWidget *parent,
	Qt::WindowFlags f
) : 
QDialog(parent, WindowTypeForStyle(style)), 
m_removeFn(0), 
m_id(-1), 
m_style(style),
m_btFlags(0) {

	if (buttons&kButton_OK)
		buttons &= ~kButton_Close;
	if (buttons&kButton_Close)
		buttons &= ~kButton_OK;

	m_btFlags = buttons;
	
	m_buttons[0] = m_buttons[1] = m_buttons[2] = 0;

	if (style == WS_Window) {
		connect(parent ? parent : MainWindow::Get(), SIGNAL(OnClose(QCloseEvent*)), SLOT(OnParentWindowClose(QCloseEvent*)));
	} 

	if (enableTick) {
		RAD_VERIFY(connect(MainWindow::Get(), SIGNAL(OnTick(float)), SLOT(OnMainWindowTick(float))));
	}

	if (deleteOnClose)
		setAttribute(Qt::WA_DeleteOnClose);

	if (style != WS_Widget) {
		setWindowFlags(
			Qt::Window|
			Qt::CustomizeWindowHint|
			Qt::WindowTitleHint|
			f
		);
	}
}

void EditorWindow::closeEvent(QCloseEvent *e) {
	e->accept();
	emit OnClose(e);
	if (e->isAccepted())
		emit Closing();
}

void EditorWindow::keyPressEvent(QKeyEvent *e) {
	if (m_style == WS_Dialog || e->key() != Qt::Key_Escape)
		QDialog::keyPressEvent(e);
}

void EditorWindow::CenterParent(float x, float y) {
	QWidget *parent = parentWidget();
	if (!parent)
		parent = MainWindow::Get()->contentBrowser->view;
	if (parent)
		Center(*parent, x, y);
}

void EditorWindow::CenterParent(int w, int h) {
	QWidget *parent = parentWidget();
	if (!parent)
		parent = MainWindow::Get()->contentBrowser->view;
	if (parent)
		Center(*parent, w, h);
}

void EditorWindow::CenterParent() {
	QWidget *parent = parentWidget();
	if (!parent)
		parent = MainWindow::Get()->contentBrowser->view;
	if (parent)
		Center(*parent);
}

void EditorWindow::Center(QWidget &widget, float x, float y) {
	PercentSize(*this, widget, x, y);
	CenterWidget(*this, widget);
}

void EditorWindow::Center(QWidget &widget, int w, int h) {
	resize(w, h);
	CenterWidget(*this, widget);
}

void EditorWindow::Center(QWidget &widget) {
	CenterWidget(*this, widget);
}

void EditorWindow::HandleOK() {
	bool accepted = true;
	emit OnOK(accepted);
	if (accepted)
		accept();
}

void EditorWindow::HandleApply() {
	bool accepted = true;
	emit OnApply(accepted);
	if (accepted) {
		emit OnApply();
		m_buttons[2]->setEnabled(false);
	}
}

void EditorWindow::SetCenterLayout(QLayout *layout) {
	CreateLayout(layout, 0);
}

void EditorWindow::SetCenterWidget(QWidget *widget) {
	CreateLayout(0, widget);
}

void EditorWindow::CreateLayout(QLayout *centerLayout, QWidget *centerWidget) {
	QVBoxLayout *outer = new QVBoxLayout(this);

	if (centerLayout) {
		outer->addLayout(centerLayout);
	} else {
		outer->addWidget(centerWidget);
	}

	if (m_btFlags&(kButton_OK|kButton_Cancel|kButton_Apply|kButton_Close)) {
		
		QHBoxLayout *buttonLayout = new QHBoxLayout();
		buttonLayout->addStretch(1);
		
		if (m_btFlags&(kButton_OK|kButton_Close)) {
			if (m_btFlags&kButton_OK) {
				m_buttons[0] = new QPushButton("OK", this);
			} else {
				m_buttons[0] = new QPushButton("Close", this);
			}

			buttonLayout->addWidget(m_buttons[0]);
			if (m_btFlags&kButton_DefaultOK)
				m_buttons[0]->setDefault(true);
			RAD_VERIFY(connect(m_buttons[0], SIGNAL(clicked()), SLOT(HandleOK())));
		}

		if (m_btFlags&kButton_Cancel) {
			m_buttons[1] = new QPushButton("Cancel", this);
			buttonLayout->addWidget(m_buttons[1]);
			if (m_btFlags&kButton_DefaultCancel)
				m_buttons[1]->setDefault(true);
			RAD_VERIFY(connect(m_buttons[1], SIGNAL(clicked()), SLOT(reject())));
		}

		if (m_btFlags&kButton_Apply) {
			m_buttons[2] = new QPushButton("Apply", this);
			buttonLayout->addWidget(m_buttons[2]);
			if (m_btFlags&kButton_DefaultApply)
				m_buttons[2]->setDefault(true);
			m_buttons[2]->setEnabled(false);
			RAD_VERIFY(connect(m_buttons[2], SIGNAL(clicked()), SLOT(HandleApply())));
		}

		outer->addLayout(buttonLayout);
	}
}

} // editor
} // tools

#include "moc_EditorWindow.cc"
