// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "setup-guide.hpp"

#include "dock-kit.hpp"
#include "../discovery/phone-browser.hpp"

#include <obs-module.h>

#include <QApplication>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPointer>
#include <QVBoxLayout>

namespace bmagicam::ui {

QWidget *setup_steps(QWidget *parent)
{
	auto steps = new QWidget(parent);
	auto layout = new QVBoxLayout(steps);
	layout->setContentsMargins(0, 0, 0, 0);

	auto text_label = new QLabel(text("Wizard.Prepare.Steps"), steps);
	text_label->setWordWrap(true);
	layout->addWidget(text_label);

	// The screenshots carry numbers and arrows but no words, so they serve every language
	auto screenshots = new QHBoxLayout();
	for (const char *file : {"iphone-1-settings.png", "iphone-2-enable.png", "iphone-3-enabled.png"}) {
		char *path = obs_module_file((std::string("images/setup/") + file).c_str());
		QPixmap image(QString::fromUtf8(path ? path : ""));
		bfree(path);
		auto label = new QLabel(steps);
		label->setPixmap(image.scaledToHeight(steps->fontMetrics().height() * 16, Qt::SmoothTransformation));
		screenshots->addWidget(label);
	}
	screenshots->addStretch();
	layout->addLayout(screenshots);
	return steps;
}

SetupGuideDialog::SetupGuideDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(text("Guide.Title"));
	auto status = new QLabel(this);
	status->setWordWrap(true);
	auto buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	auto layout = new QVBoxLayout(this);
	layout->addWidget(setup_steps(this));
	layout->addWidget(status);
	layout->addWidget(buttons);

	const auto refresh = [status] {
		const auto phones = PhoneBrowser::instance().phones();
		status->setText(
			phones.empty() ? text("Wizard.Prepare.Looking")
				       : text("Wizard.Prepare.Found").arg(QString::fromStdString(phones.front().name)));
	};
	refresh();
	listener_ = PhoneBrowser::instance().listen([guard = QPointer<QLabel>(status), refresh] {
		QMetaObject::invokeMethod(
			qApp,
			[guard, refresh] {
				if (guard)
					refresh();
			},
			Qt::QueuedConnection);
	});
}

SetupGuideDialog::~SetupGuideDialog()
{
	PhoneBrowser::instance().unlisten(listener_);
}

} // namespace bmagicam::ui
