// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "remote-dialog.hpp"

#include "dock-kit.hpp"
#include "../remote/access.hpp"
#include "../remote/remote-server.hpp"
#include "../remote/remote-settings.hpp"

#include <qrcodegen.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace bmagicam::ui {

namespace {

// A QR code as an image, dark modules on light with the quiet zone scanners need
QPixmap qr_pixmap(const QString &content, int size)
{
	const qrcodegen::QrCode code =
		qrcodegen::QrCode::encodeText(content.toUtf8().constData(), qrcodegen::QrCode::Ecc::MEDIUM);
	const int border = 4;
	const int modules = code.getSize() + 2 * border;
	QImage image(modules, modules, QImage::Format_RGB32);
	image.fill(Qt::white);
	for (int y = 0; y < code.getSize(); y++) {
		for (int x = 0; x < code.getSize(); x++) {
			if (code.getModule(x, y))
				image.setPixel(x + border, y + border, qRgb(0, 0, 0));
		}
	}
	return QPixmap::fromImage(image.scaled(size, size, Qt::KeepAspectRatio, Qt::FastTransformation));
}

// Connect info: the panel's address, the password, and a QR code that opens the panel signed in (WEB-5)
class ConnectInfo : public QDialog {
public:
	ConnectInfo(QWidget *parent, int port, const QString &password) : QDialog(parent), password_(password)
	{
		setWindowTitle(text("Remote.ConnectInfo.Title"));
		auto layout = new QVBoxLayout(this);
		const QStringList addresses = panel_addresses(port);
		if (addresses.isEmpty()) {
			layout->addWidget(new QLabel(text("Remote.NoAddress"), this));
		} else {
			auto form = new QFormLayout();
			addresses_ = new QComboBox(this);
			addresses_->addItems(addresses);
			form->addRow(text("Remote.ConnectInfo.Address"), addresses_);
			if (!password.isEmpty()) {
				auto shown = new QLineEdit(password, this);
				shown->setReadOnly(true);
				shown->setMinimumWidth(fontMetrics().horizontalAdvance(password) +
						       fontMetrics().height() * 2);
				form->addRow(text("Remote.Password"), shown);
			}
			layout->addLayout(form);
			code_ = new QLabel(this);
			code_->setAlignment(Qt::AlignCenter);
			layout->addWidget(code_);
			auto scan = new QLabel(text("Remote.ConnectInfo.Scan"), this);
			scan->setWordWrap(true);
			layout->addWidget(scan);
			connect(addresses_, &QComboBox::currentTextChanged, this, [this] { refresh(); });
			refresh();
		}
		auto buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
		connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
		layout->addWidget(buttons);
	}

private:
	void refresh()
	{
		// The fragment never reaches the server; the panel reads the password from it
		QString address = addresses_->currentText();
		if (!password_.isEmpty())
			address += QStringLiteral("#password=") + QString::fromUtf8(QUrl::toPercentEncoding(password_));
		code_->setPixmap(qr_pixmap(address, fontMetrics().height() * 14));
	}

	QString password_;
	QComboBox *addresses_ = nullptr;
	QLabel *code_ = nullptr;
};

} // namespace

QStringList panel_addresses(int port)
{
	QStringList preferred;
	QStringList others;
	for (const QNetworkInterface &interface : QNetworkInterface::allInterfaces()) {
		const auto flags = interface.flags();
		if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning) ||
		    (flags & QNetworkInterface::IsLoopBack))
			continue;
		for (const QNetworkAddressEntry &entry : interface.addressEntries()) {
			const QHostAddress ip = entry.ip();
			if (ip.protocol() != QAbstractSocket::IPv4Protocol)
				continue;
			const std::string text_ip = ip.toString().toStdString();
			if (!is_local_address(text_ip) || text_ip.rfind("169.254.", 0) == 0)
				continue;
			const QString url = QStringLiteral("http://%1:%2/").arg(ip.toString()).arg(port);
			// Home and office networks first; carrier-grade and VPN ranges after
			(text_ip.rfind("192.168.", 0) == 0 || text_ip.rfind("10.", 0) == 0 ? preferred : others)
				.push_back(url);
		}
	}
	return preferred + others;
}

nlohmann::json theme_palette()
{
	const QPalette palette = QApplication::palette();
	const auto color = [&palette](QPalette::ColorRole role, QPalette::ColorGroup group = QPalette::Active) {
		return palette.color(group, role).name().toStdString();
	};
	return {{"window", color(QPalette::Window)},
		{"windowText", color(QPalette::WindowText)},
		{"base", color(QPalette::Base)},
		{"alternateBase", color(QPalette::AlternateBase)},
		{"text", color(QPalette::Text)},
		{"button", color(QPalette::Button)},
		{"buttonText", color(QPalette::ButtonText)},
		{"highlight", color(QPalette::Highlight)},
		{"highlightedText", color(QPalette::HighlightedText)},
		{"mid", color(QPalette::Mid)},
		{"placeholder", color(QPalette::PlaceholderText)},
		{"disabledText", color(QPalette::WindowText, QPalette::Disabled)},
		{"dark", palette.color(QPalette::Window).lightnessF() < 0.5}};
}

RemoteDialog::RemoteDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(text("Remote.Title"));
	const RemoteSettings settings = load_remote_settings();

	auto layout = new QVBoxLayout(this);
	auto form = new QFormLayout();
	enabled_ = new QCheckBox(text("Remote.Enable"), this);
	enabled_->setObjectName("remoteEnabled");
	enabled_->setChecked(settings.enabled);
	enabled_->setToolTip(text("Remote.Enable.Tooltip"));
	form->addRow(enabled_);

	port_ = new QSpinBox(this);
	port_->setRange(1024, 65535);
	port_->setValue(settings.port);
	form->addRow(text("Remote.Port"), port_);

	authentication_ = new QCheckBox(text("Remote.Authentication"), this);
	authentication_->setChecked(settings.authentication);
	authentication_->setToolTip(text("Remote.Authentication.Tooltip"));
	form->addRow(authentication_);

	auto password_row = new QHBoxLayout();
	password_ = new QLineEdit(QString::fromStdString(settings.password), this);
	password_->setEchoMode(QLineEdit::Password);
	show_password_ = new QPushButton(text("Remote.Show"), this);
	auto copy = new QPushButton(text("Remote.Copy"), this);
	auto generate = new QPushButton(text("Remote.Generate"), this);
	password_row->addWidget(password_, 1);
	password_row->addWidget(show_password_);
	password_row->addWidget(copy);
	password_row->addWidget(generate);
	form->addRow(text("Remote.Password"), password_row);
	layout->addLayout(form);

	auto connect_info = new QPushButton(text("Remote.ConnectInfo"), this);
	connect_info->setObjectName("showConnectInfo");
	layout->addWidget(connect_info, 0, Qt::AlignLeft);
	auto note = new QLabel(text("Remote.Note"), this);
	note->setWordWrap(true);
	set_theme_class(note, "text-muted");
	layout->addWidget(note);
	status_ = new QLabel(this);
	status_->setWordWrap(true);
	layout->addWidget(status_);

	auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	layout->addWidget(buttons);

	connect(show_password_, &QPushButton::clicked, this, [this] {
		const bool hidden = password_->echoMode() == QLineEdit::Password;
		password_->setEchoMode(hidden ? QLineEdit::Normal : QLineEdit::Password);
		show_password_->setText(text(hidden ? "Remote.Hide" : "Remote.Show"));
	});
	connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(password_->text()); });
	connect(generate, &QPushButton::clicked, this,
		[this] { password_->setText(QString::fromStdString(generate_password())); });
	connect(authentication_, &QCheckBox::toggled, password_, &QWidget::setEnabled);
	connect(connect_info, &QPushButton::clicked, this, [this] { show_connect_info(); });
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	password_->setEnabled(settings.authentication);
	refresh_status();
}

void RemoteDialog::refresh_status()
{
	const remote::Server &server = remote::Server::instance();
	if (server.running())
		status_->setText(
			text("Remote.Running").arg(panel_addresses(port_->value()).value(0, QStringLiteral("—"))));
	else if (!server.error().empty())
		status_->setText(text("Remote.Failed").arg(QString::fromStdString(server.error())));
	else
		status_->setText(text("Remote.Stopped"));
}

void RemoteDialog::accept()
{
	RemoteSettings settings;
	settings.enabled = enabled_->isChecked();
	settings.port = port_->value();
	settings.authentication = authentication_->isChecked();
	settings.password = password_->text().trimmed().toStdString();
	if (settings.password.empty())
		settings.password = generate_password();
	save_remote_settings(settings);
	if (!remote::Server::instance().apply(settings)) {
		refresh_status();
		QMessageBox::warning(
			this, text("Remote.Title"),
			text("Remote.Failed").arg(QString::fromStdString(remote::Server::instance().error())));
		return;
	}
	QDialog::accept();
}

void RemoteDialog::show_connect_info()
{
	ConnectInfo info(this, port_->value(), authentication_->isChecked() ? password_->text() : QString());
	info.exec();
}

} // namespace bmagicam::ui
