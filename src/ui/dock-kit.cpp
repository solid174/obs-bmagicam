// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "dock-kit.hpp"

#include <obs-module.h>

#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace bmagicam::ui {

namespace {

// How long after the user's last change the phone's values are ignored
constexpr auto kEchoWindow = std::chrono::milliseconds(300);

} // namespace

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

void set_theme_class(QWidget *widget, const char *theme_class)
{
	if (widget->property("class").toString() == QLatin1String(theme_class))
		return;
	widget->setProperty("class", theme_class);
	widget->style()->unpolish(widget);
	widget->style()->polish(widget);
}

Row::Row(const QString &title, const QString &tooltip, QWidget *parent) : QWidget(parent), tooltip_(tooltip)
{
	auto label = new QLabel(title, this);
	label->setToolTip(tooltip);
	label->setWordWrap(true);

	header_ = new QHBoxLayout();
	header_->setContentsMargins(0, 0, 0, 0);
	header_->addWidget(label, 1);

	layout_ = new QVBoxLayout(this);
	layout_->setContentsMargins(0, 0, 0, 0);
	layout_->setSpacing(fontMetrics().height() / 4);
	layout_->addLayout(header_);
}

void Row::add_leading(QWidget *widget)
{
	if (widget->toolTip().isEmpty())
		widget->setToolTip(tooltip_);
	header_->insertWidget(0, widget);
}

void Row::add_extra(QWidget *widget)
{
	if (widget->toolTip().isEmpty())
		widget->setToolTip(tooltip_);
	header_->addWidget(widget);
}

void Row::set_control(QWidget *control)
{
	if (control->toolTip().isEmpty())
		control->setToolTip(tooltip_);
	layout_->addWidget(control);
}

void EditGuard::touched()
{
	last_ = std::chrono::steady_clock::now();
}

bool EditGuard::settled(QObject *context, const std::function<void()> &retry)
{
	const auto quiet_at = last_ + kEchoWindow;
	const auto now = std::chrono::steady_clock::now();
	if (now >= quiet_at)
		return true;
	if (!retry_scheduled_) {
		retry_scheduled_ = true;
		const auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(quiet_at - now) +
				  std::chrono::milliseconds(10);
		QTimer::singleShot(static_cast<int>(wait.count()), context, [this, retry] {
			retry_scheduled_ = false;
			retry();
		});
	}
	return false;
}

void Watches::add(std::vector<std::string> paths, std::function<void()> refresh)
{
	watches_.push_back({std::move(paths), std::move(refresh)});
}

void Watches::refresh(const std::set<std::string> &paths) const
{
	for (const Watch &watch : watches_) {
		const bool shown = paths.empty() ||
				   std::any_of(watch.paths.begin(), watch.paths.end(),
					       [&](const std::string &path) { return paths.count(path); });
		if (shown)
			watch.refresh();
	}
}

} // namespace bmagicam::ui
