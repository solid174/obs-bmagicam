// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "controls-dock.hpp"

#include <obs-module.h>

#include <QLabel>
#include <QVBoxLayout>

namespace bmagicam::ui {

ControlsDock::ControlsDock(QWidget *parent) : QWidget(parent)
{
	setMinimumWidth(280);

	auto hint = new QLabel(QString::fromUtf8(obs_module_text("Dock.Empty")), this);
	hint->setProperty("class", "text-muted");
	hint->setAlignment(Qt::AlignCenter);
	hint->setWordWrap(true);

	auto layout = new QVBoxLayout(this);
	layout->addStretch();
	layout->addWidget(hint);
	layout->addStretch();
}

} // namespace bmagicam::ui
