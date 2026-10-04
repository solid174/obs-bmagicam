// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <nlohmann/json.hpp>

#include <QDialog>
#include <QStringList>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace bmagicam::ui {

// Tools → iPhone Camera Remote Control, laid out like OBS's WebSocket Server Settings (WEB-5)
class RemoteDialog : public QDialog {
public:
	explicit RemoteDialog(QWidget *parent);

	void accept() override;

private:
	void refresh_status();
	void show_connect_info();

	QCheckBox *enabled_;
	QSpinBox *port_;
	QCheckBox *authentication_;
	QLineEdit *password_;
	QPushButton *show_password_;
	QLabel *status_;
};

// The panel's addresses on this computer's local networks, the most likely first
QStringList panel_addresses(int port);
// The OBS theme's colors, for the web panel (/api/v1/theme)
nlohmann::json theme_palette();

} // namespace bmagicam::ui
