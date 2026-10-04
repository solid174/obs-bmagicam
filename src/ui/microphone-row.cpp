// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "microphone-row.hpp"

#include "widgets/flow-layout.hpp"
#include "../sync/microphone-sync.hpp"

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <cmath>
#include <vector>

namespace bmagicam::ui {

namespace {

constexpr int kSourcesCheckMs = 2000;

struct Microphone {
	QString name;
	QString uuid;
};

std::vector<Microphone> microphones()
{
	std::vector<Microphone> found;
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			if (is_computer_microphone(source))
				static_cast<std::vector<Microphone> *>(param)->push_back(
					{QString::fromUtf8(obs_source_get_name(source)),
					 QString::fromUtf8(obs_source_get_uuid(source))});
			return true;
		},
		&found);
	return found;
}

} // namespace

MicrophoneRow::MicrophoneRow(PanelContext context, QWidget *parent)
	: Row(text("Dock.Microphone"), text("Dock.Microphone.Tooltip"), parent),
	  context_(std::move(context))
{
	auto controls = new QWidget(this);
	auto layout = new FlowLayout(controls);
	microphones_ = new QComboBox(controls);
	microphones_->setObjectName("microphones");
	microphones_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	microphones_->setMinimumContentsLength(10);
	sync_ = new QPushButton(text("Dock.Microphone.Sync"), controls);
	sync_->setObjectName("syncMicrophone");
	sync_->setToolTip(text("Dock.Microphone.Sync.Tooltip"));
	undo_ = new QPushButton(text("Dock.Microphone.Undo"), controls);
	undo_->setObjectName("undoMicrophoneSync");
	undo_->setToolTip(text("Dock.Microphone.Undo.Tooltip"));
	undo_->hide();
	layout->addWidget(microphones_);
	layout->addWidget(sync_);
	layout->addWidget(undo_);
	// Who needs this row and who does not, without hovering
	auto hint = new QLabel(text("Dock.Microphone.Hint"), this);
	hint->setWordWrap(true);
	set_theme_class(hint, "text-muted");
	set_control(hint);
	set_control(controls);
	result_ = new QLabel(this);
	result_->setWordWrap(true);
	set_theme_class(result_, "text-muted");
	result_->hide();
	set_control(result_);

	connect(sync_, &QPushButton::clicked, this, [this] { start(); });
	connect(undo_, &QPushButton::clicked, this, [this] { undo(); });
	connect(&sources_timer_, &QTimer::timeout, this, [this] { refresh_sources(); });
	sources_timer_.start(kSourcesCheckMs);
	listen_timer_.setSingleShot(true);
	connect(&listen_timer_, &QTimer::timeout, this, [this] { finish(); });
	refresh_sources();
}

MicrophoneRow::~MicrophoneRow() = default;

void MicrophoneRow::refresh_sources()
{
	const std::vector<Microphone> sources = microphones();
	const QString chosen = microphones_->currentData().toString();
	bool same = static_cast<int>(sources.size()) == microphones_->count();
	for (size_t index = 0; same && index < sources.size(); index++)
		same = microphones_->itemData(static_cast<int>(index)).toString() == sources[index].uuid &&
		       microphones_->itemText(static_cast<int>(index)) == sources[index].name;
	if (!same) {
		microphones_->clear();
		for (const Microphone &source : sources)
			microphones_->addItem(source.name, source.uuid);
		const int index = microphones_->findData(chosen);
		microphones_->setCurrentIndex(index >= 0 ? index : 0);
	}
	setVisible(!sources.empty());
}

void MicrophoneRow::start()
{
	OBSSourceAutoRelease camera = obs_weak_source_get_source(context_.source);
	OBSSourceAutoRelease microphone =
		obs_get_source_by_uuid(microphones_->currentData().toString().toUtf8().constData());
	if (!camera || !microphone)
		return;
	listening_ = std::make_unique<MicrophoneSync>(camera, microphone);
	listen_timer_.start(static_cast<int>(MicrophoneSync::kListenTime.count() * 1000));
	sync_->setEnabled(false);
	microphones_->setEnabled(false);
	sync_->setText(text("Dock.Microphone.Listening"));
	result_->setText(text("Dock.Microphone.Talk"));
	result_->show();
}

void MicrophoneRow::finish()
{
	if (!listening_)
		return;
	const MicrophoneSync::Outcome outcome = listening_->finish();
	listening_.reset();
	sync_->setEnabled(true);
	microphones_->setEnabled(true);
	sync_->setText(text("Dock.Microphone.Sync"));

	OBSSourceAutoRelease microphone =
		obs_get_source_by_uuid(microphones_->currentData().toString().toUtf8().constData());
	if (outcome.result != MicrophoneSync::Result::Measured || !microphone) {
		result_->setText(text(outcome.result == MicrophoneSync::Result::Silence ? "Dock.Microphone.Silence"
											: "Dock.Microphone.Unclear"));
		return;
	}
	// The previous offset is kept for Undo (SYN-2)
	undo_source_ = OBSGetWeakRef(microphone);
	undo_offset_ = obs_source_get_sync_offset(microphone);
	obs_source_set_sync_offset(microphone, outcome.lag_ns);
	const long milliseconds = std::lround(static_cast<double>(outcome.lag_ns) / 1e6);
	sync_->setText(text("Dock.Microphone.Synced").arg(milliseconds));
	result_->setText(text("Dock.Microphone.Applied").arg(microphones_->currentText()).arg(milliseconds));
	undo_->show();
}

void MicrophoneRow::undo()
{
	if (OBSSourceAutoRelease microphone = obs_weak_source_get_source(undo_source_)) {
		obs_source_set_sync_offset(microphone, undo_offset_);
		result_->setText(text("Dock.Microphone.Undone")
					 .arg(QString::fromUtf8(obs_source_get_name(microphone)))
					 .arg(std::lround(static_cast<double>(undo_offset_) / 1e6)));
	}
	undo_->hide();
	sync_->setText(text("Dock.Microphone.Sync"));
}

} // namespace bmagicam::ui
