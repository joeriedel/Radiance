// EditorCookerDialog.cpp
// Copyright (c) 2010 Sunside Inc., All Rights Reserved
// Author: Joe Riedel
// See Radiance/LICENSE for licensing terms.

#include RADPCH
#include "EditorCookerDialog.h"
#include "EditorUtils.h"
#include "EditorMainWindow.h"
#include "EditorGLWidget.h"
#include "../../App.h"
#include <Runtime/Tokenizer.h>
#include <Runtime/DataCodec/ZLib.h>
#include <Runtime/File.h>
#include <QtCore/QSignalMapper>
#include <QtGui/QPlainTextEdit>
#include <QtGui/QCheckBox>
#include <QtGui/QRadioButton>
#include <QtGui/QVBoxLayout>
#include <QtGui/QHBoxLayout>
#include <QtGui/QGridLayout>
#include <QtGui/QPushButton>
#include <QtGui/QMessageBox>
#include <QtGui/QGroupBox>
#include <QtGui/QSlider>
#include <QtGui/QLabel>
#include <QtGui/QLineEdit>
#include <QtGui/QIntValidator>
#include <QtOpenGL/QGLFormat>
#include <QtOpenGL/QGLContext>
#include <QtGui/QDesktopWidget>

namespace tools {
namespace editor {

QMutex CookerDialog::s_m;
CookerDialog *CookerDialog::s_instance = 0;

CookerDialog::CookerDialog(QWidget *parent) : 
QDialog(parent),
m_platforms(0),
m_textArea(0),
m_cook(0),
m_thread(0),
m_clean(0),
m_scriptsOnly(0),
m_fast(0),
m_closeWhenFinished(false) {
	m_stringBuf.reset(new (ZEditor) CookerStringBuf(this));
	m_oStream.reset(new (ZEditor) std::ostream(m_stringBuf.get()));

	setWindowTitle("Cooker");
	QDesktopWidget *desktop = QApplication::desktop();
	resize(517, desktop->screenGeometry().height()*0.6f);

	setWindowFlags(
		Qt::Dialog|
		Qt::CustomizeWindowHint|
		Qt::WindowTitleHint|
		Qt::WindowCloseButtonHint
	);

	setAttribute(Qt::WA_DeleteOnClose);

	if (parent) {
		setModal(true);
		CenterWidget(*this, *parent);
	}

	QVBoxLayout *vbox = new (ZEditor) QVBoxLayout(this);

	m_textArea = new (ZEditor) QPlainTextEdit(this);
	m_textArea->setUndoRedoEnabled(false);
	m_textArea->setLineWrapMode(QPlainTextEdit::NoWrap);
	m_textArea->setReadOnly(true);
	vbox->addWidget(m_textArea);

	MainWindow *mainWin = MainWindow::Get();
	QHBoxLayout *hbox = new (ZEditor) QHBoxLayout();
	vbox->addLayout(hbox);

	QVBoxLayout *vGroupBox = new (ZEditor) QVBoxLayout();

	QGroupBox *targetGroup = new (ZEditor) QGroupBox("Targets");
	QGridLayout *grid = new (ZEditor) QGridLayout(targetGroup);

	QHBoxLayout *hbox2 = new (ZEditor) QHBoxLayout();
	hbox2->addWidget(targetGroup);
	
	m_multiThreaded = new (ZEditor) QGroupBox("Multithreaded");
	
	m_multiThreaded->setCheckable(true);
	m_multiThreaded->setChecked(mainWin->userPrefs->value("cook/multithreaded", false).toBool());
	m_numThreads = new (ZEditor) QLineEdit();
	m_numThreads->setText(mainWin->userPrefs->value("cook/numthreads", thread::NumContexts()).toString());
	m_numThreads->setEnabled(m_multiThreaded->isChecked());
	m_numThreads->setValidator(new QIntValidator(0, 256, this));
	m_numThreads->setMaximumWidth(25);
	RAD_VERIFY(connect(m_multiThreaded, SIGNAL(clicked(bool)), SLOT(MultithreadedClicked(bool))));
	RAD_VERIFY(connect(m_numThreads, SIGNAL(textChanged(const QString&)), SLOT(NumThreadsChanged(const QString&))));
	
	QHBoxLayout *hbox3 = new (ZEditor) QHBoxLayout(m_multiThreaded);
	hbox3->addStretch(1);
	hbox3->addWidget(new QLabel("Num Threads:"));
	hbox3->addWidget(m_numThreads);
	hbox3->addStretch(1);

	hbox2->addWidget(m_multiThreaded);
	hbox2->addStretch(1);
	
	vGroupBox->addLayout(hbox2);
	hbox->addLayout(vGroupBox);

	m_platforms = 0;
	int targetNum = 0;

	enum { MaxColumns = 3 };

	QRadioButton *checkedRadio = 0;

	for (int i = pkg::P_FirstTarget; i <= pkg::P_LastTarget; i <<= 1, ++targetNum) {
		QString name(pkg::PlatformNameForFlags(i));

		QRadioButton *radio = new (ZEditor) QRadioButton(name);
		
		bool checked = mainWin->userPrefs->value("cook/" + name, false).toBool();
		radio->setProperty("platformFlag", i);

		int col = targetNum / MaxColumns;
		int row = targetNum % MaxColumns;

		grid->addWidget(radio, row, col);

		if (checked) {
			m_platforms = i;
			checkedRadio = radio;
		}

		RAD_VERIFY(connect(radio, SIGNAL(toggled(bool)), SLOT(OnPlatformSelected(bool))));
	}

	QVBoxLayout *buttonLayout = new (ZEditor) QVBoxLayout();
	hbox->addLayout(buttonLayout);
	hbox->setStretch(0, 1);

	m_clean = new (ZEditor) QCheckBox("Clean");
	m_clean->setChecked(
		mainWin->userPrefs->value("cook/clean", true).toBool() ?
		Qt::Checked :
		Qt::Unchecked
	);
	RAD_VERIFY(connect(m_clean, SIGNAL(stateChanged(int)), SLOT(CleanChecked(int))));
	buttonLayout->addWidget(m_clean);
	
	m_scriptsOnly = new (ZEditor) QCheckBox("Scripts Only");
	m_scriptsOnly->setChecked(
		mainWin->userPrefs->value("cook/scriptsOnly", false).toBool() ?
		Qt::Checked :
		Qt::Unchecked
	);
	RAD_VERIFY(connect(m_scriptsOnly, SIGNAL(stateChanged(int)), SLOT(ScriptsOnly(int))));
	buttonLayout->addWidget(m_scriptsOnly);

	m_fast = new (ZEditor) QCheckBox("Fast Cook (Low Quality)");
	m_fast->setChecked(
		mainWin->userPrefs->value("cook/fast", false).toBool() ?
		Qt::Checked :
		Qt::Unchecked
	);
	RAD_VERIFY(connect(m_fast, SIGNAL(stateChanged(int)), SLOT(FastCook(int))));
	buttonLayout->addWidget(m_fast);

	if (m_scriptsOnly->checkState() == Qt::Checked) {
		m_clean->setChecked(Qt::Unchecked);
		m_clean->setEnabled(false);
	}

	if (m_clean->checkState() == Qt::Checked) {
		m_scriptsOnly->setChecked(Qt::Unchecked);
		m_scriptsOnly->setEnabled(false);
	}
	
	m_cook = new (ZEditor) QPushButton("Cook...");
	m_cook->resize(30, 20);
	m_cook->setEnabled(m_platforms != 0);
	RAD_VERIFY(connect(m_cook, SIGNAL(clicked()), SLOT(CookClicked())));
	buttonLayout->addWidget(m_cook);
	buttonLayout->setStretch(0, 1);

	s_instance = this;

	m_glw = new (ZEditor) GLWidget(this);
	m_glw->hide();

	if (checkedRadio)
		checkedRadio->setChecked(true);
}

CookerDialog::~CookerDialog() {
	QMutexLocker L(&s_m);
	s_instance = 0;
}

void CookerDialog::OnPlatformSelected(bool checked) {
	QGroupBox *group = static_cast<QGroupBox*>(sender());
	int flags = group->property("platformFlag").toInt();

	if (checked) {
		MainWindow::Get()->userPrefs->setValue(QString("cook/") + pkg::PlatformNameForFlags(flags), true);
		m_platforms = flags;
		m_cook->setEnabled(true);
	} else {
		MainWindow::Get()->userPrefs->setValue(QString("cook/") + pkg::PlatformNameForFlags(flags), false);
	}
}

void CookerDialog::CookClicked() {
	if (m_thread) {
		if (QMessageBox::question(
			this,
			"Question",
			"Are you sure you want to abort the current build?",
			QMessageBox::Yes|QMessageBox::No
		) == QMessageBox::Yes) {
			m_thread->Cancel();
		}

		return;
	}

	int numThreads = 0;
	if (m_multiThreaded->isChecked()) {
		numThreads = m_numThreads->text().toInt();
		if (numThreads < 0)
			numThreads = 0;
	}

	int enabledLangMask;
	App::Get()->LoadLangId(&enabledLangMask, App::Get()->systemLangId);
	if (enabledLangMask == 0) {
		QMessageBox::critical(
			this,
			"Error",
			"languages.txt doesn't contain any valid languages!"
		);
		return;
	}

	m_textArea->clear();
	m_cook->setText("Cancel...");

	int flags = m_platforms;
	if (m_clean->checkState() == Qt::Checked)
		flags |= pkg::P_Clean;
	if (m_scriptsOnly->checkState() == Qt::Checked)
		flags |= pkg::P_ScriptsOnly;
	if (m_fast->checkState() == Qt::Checked)
		flags |= pkg::P_FastCook;

	m_thread = new CookThread(
		m_glw,
		flags,
		enabledLangMask,
		0,
		numThreads,
		*m_oStream
	);

	RAD_VERIFY(connect(m_thread, SIGNAL(Finished()), SLOT(CookFinished())));

	App::Get()->engine->sys->packages->ResetCancelCook();
	MainWindow::Get()->tickEnabled = false;
	m_thread->start();
}

void CookerDialog::CookFinished() {
	delete m_thread;
	m_thread = 0;
	m_cook->setText("Cook...");
	MainWindow::Get()->tickEnabled = true;

	if (m_closeWhenFinished)
		close();
	else
		QApplication::beep();
}

void CookerDialog::CompressionChanged(int value) {
	MainWindow::Get()->userPrefs->setValue("cook/compression", value);
}

void CookerDialog::CleanChecked(int value) {
	MainWindow::Get()->userPrefs->setValue("cook/clean", value == Qt::Checked);
	if (m_scriptsOnly) {
		if (m_clean->checkState() == Qt::Checked) {
			m_scriptsOnly->setChecked(Qt::Unchecked);
			m_scriptsOnly->setEnabled(false);
		} else {
			m_scriptsOnly->setEnabled(true);
		}
	}
}

void CookerDialog::ScriptsOnly(int value) {
	MainWindow::Get()->userPrefs->setValue("cook/scriptsOnly", value == Qt::Checked);

	if (m_clean) {
		if (m_scriptsOnly->checkState() == Qt::Checked) {
			m_clean->setChecked(Qt::Unchecked);
			m_clean->setEnabled(false);
		} else {
			m_clean->setEnabled(true);
		}
	}
}

void CookerDialog::FastCook(int value) {
	MainWindow::Get()->userPrefs->setValue("cook/fast", value == Qt::Checked);
}

void CookerDialog::MultithreadedClicked(bool checked) {
	MainWindow::Get()->userPrefs->setValue("cook/multithreaded", checked);
	m_numThreads->setEnabled(checked);
}

void CookerDialog::NumThreadsChanged(const QString &text) {
	MainWindow::Get()->userPrefs->setValue("cook/numthreads", text);
}

void CookerDialog::closeEvent(QCloseEvent *e) {
	if (m_thread) {
		e->ignore();
		if (m_closeWhenFinished)
			return;
		if (QMessageBox::question(
			this, 
			"Question", 
			"Are you sure you want to abort the current build?", 
			QMessageBox::Yes|QMessageBox::No
		) == QMessageBox::Yes) {
			m_closeWhenFinished = true;
			m_thread->Cancel();
		}
	} else {
		e->accept();
	}
}

void CookerDialog::SPrint(const char *msg) {
	QMutexLocker l(&s_m);
	if (s_instance)
		s_instance->Print(msg);
}

void CookerDialog::Print(const char *msg) {
	QCoreApplication::postEvent(this, new (ZEditor) PrintMsgEvent(msg));
}

void CookerDialog::customEvent(QEvent *e) {
	switch (e->type()) {
	case EV_CookWindowPrint:
		OnPrintMsg(*static_cast<PrintMsgEvent*>(e));
		break;
	default:
		break;
	}
}

void CookerDialog::OnPrintMsg(const PrintMsgEvent &msg) {
	QTextCursor c = m_textArea->textCursor();
	c.movePosition(QTextCursor::End, QTextCursor::MoveAnchor);
	m_textArea->setTextCursor(c);
	m_textArea->insertPlainText(msg.msg.c_str.get());
}

CookThread::CookThread(
	GLWidget *glw,
	int plats,
	int languages,
	int compression,
	int numThreads,
	std::ostream &cout
) :
m_glw(glw),
m_plats(plats), 
m_languages(languages),
m_compression(compression),
m_numThreads(numThreads),
m_cout(&cout) {
}

void CookThread::Cancel() {
	App::Get()->engine->sys->packages->CancelCook();
}

void CookThread::run() {
	m_glw->bindGL(true);
	App::Get()->engine->sys->packages->Cook(m_plats, m_languages, m_compression, m_numThreads, *m_glw, *m_cout);
	emit Finished();
}
	
} // editor
} // tools

#include "moc_EditorCookerDialog.cc"
