#pragma once

class QToolBar;
class QToolButton;
class QPushButton;
class QTabBar;

// Main-window spacing; native control painting and command ownership stay intact.
void QtConfigureMainToolbar(QToolBar &toolbar);
void QtConfigureMainToolbarButton(QToolButton &button);
void QtConfigurePanelParentButton(QPushButton &button);
void QtConfigureChromeToolButton(QToolButton &button);
void QtConfigureWorkspaceTabs(QTabBar &tabs);
