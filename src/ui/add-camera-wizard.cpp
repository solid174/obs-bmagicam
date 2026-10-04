// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "add-camera-wizard.hpp"

#include "controls-dock.hpp"
#include "frontend.hpp"
#include "setup-guide.hpp"
#include "../camera/look-library.hpp"
#include "../camera/stream-presets.hpp"
#include "../discovery/phone-browser.hpp"
#include "../source/camera-source.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <obs.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QDockWidget>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPushButton>
#include <QRadioButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWizardPage>

#include <memory>

namespace bmagicam::ui {

namespace {

enum PageId { kPreparePage, kChoosePage, kPicturePage };

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

// Calls back on the Qt thread whenever the list of phones changes, until the owner is gone
class PhonesWatcher {
public:
	template<typename Callback> explicit PhonesWatcher(Callback callback)
	{
		listener_ = PhoneBrowser::instance().listen([callback, alive = std::weak_ptr<int>(alive_)] {
			QMetaObject::invokeMethod(
				qApp,
				[callback, alive] {
					if (alive.lock())
						callback();
				},
				Qt::QueuedConnection);
		});
	}

	~PhonesWatcher() { PhoneBrowser::instance().unlisten(listener_); }

private:
	std::shared_ptr<int> alive_ = std::make_shared<int>();
	int listener_ = 0;
};

class PreparePage final : public QWizardPage {
public:
	explicit PreparePage(AddCameraWizard *wizard) : wizard_(wizard)
	{
		setTitle(text("Wizard.Prepare.Title"));

		status_ = new QLabel(this);
		status_->setWordWrap(true);

		auto manual = new QPushButton(text("Wizard.Prepare.Manual"), this);
		manual->setFlat(true);
		connect(manual, &QPushButton::clicked, this, [this] {
			manual_ = true;
			wizard_->next();
		});

		auto layout = new QVBoxLayout(this);
		layout->addWidget(setup_steps(this));
		layout->addWidget(status_);
		layout->addWidget(manual, 0, Qt::AlignLeft);

		watcher_ = std::make_unique<PhonesWatcher>([this] { refresh(); });
		refresh();
	}

	bool isComplete() const override { return phones_ > 0; }

	void initializePage() override { refresh(); }

	int nextId() const override
	{
		// One phone needs no choosing
		if (!manual_ && phones_ == 1)
			return kPicturePage;
		return kChoosePage;
	}

	bool validatePage() override
	{
		const auto phones = PhoneBrowser::instance().phones();
		if (!manual_ && phones.size() == 1) {
			wizard_->choice.phone_id = phones.front().id;
			wizard_->choice.phone_name = phones.front().name;
		}
		return true;
	}

private:
	void refresh()
	{
		const auto phones = PhoneBrowser::instance().phones();
		phones_ = phones.size();
		if (phones.empty())
			status_->setText(text("Wizard.Prepare.Looking"));
		else
			status_->setText(text("Wizard.Prepare.Found").arg(QString::fromStdString(phones.front().name)));
		emit completeChanged();

		// When the phone shows up, the wizard moves on by itself
		if (phones_ == 1 && !moved_on_ && wizard_->currentId() == kPreparePage) {
			moved_on_ = true;
			QTimer::singleShot(0, wizard_, [wizard = wizard_] { wizard->next(); });
		}
	}

	AddCameraWizard *wizard_;
	QLabel *status_;
	std::unique_ptr<PhonesWatcher> watcher_;
	size_t phones_ = 0;
	bool manual_ = false;
	bool moved_on_ = false;
};

class ChoosePage final : public QWizardPage {
public:
	explicit ChoosePage(AddCameraWizard *wizard) : wizard_(wizard)
	{
		setTitle(text("Wizard.Choose.Title"));

		phones_ = new QListWidget(this);
		manual_ = new QRadioButton(text("Wizard.Choose.Manual"), this);
		address_ = new QLineEdit(this);
		address_->setPlaceholderText("192.168.1.23");

		connect(phones_, &QListWidget::currentRowChanged, this, [this](int row) {
			if (row >= 0)
				manual_->setChecked(false);
			emit completeChanged();
		});
		connect(manual_, &QRadioButton::toggled, this, [this](bool on) {
			if (on)
				phones_->setCurrentRow(-1);
			emit completeChanged();
		});
		connect(address_, &QLineEdit::textChanged, this, [this] {
			manual_->setChecked(true);
			emit completeChanged();
		});

		auto layout = new QVBoxLayout(this);
		layout->addWidget(phones_);
		layout->addWidget(manual_);
		layout->addWidget(address_);

		watcher_ = std::make_unique<PhonesWatcher>([this] { refresh(); });
		refresh();
	}

	bool isComplete() const override
	{
		return phones_->currentRow() >= 0 || (manual_->isChecked() && !address_->text().trimmed().isEmpty());
	}

	int nextId() const override { return kPicturePage; }

	bool validatePage() override
	{
		AddCameraWizard::Choice &choice = wizard_->choice;
		if (manual_->isChecked()) {
			choice.phone_id.clear();
			choice.phone_name.clear();
			choice.address = address_->text().trimmed().toStdString();
		} else if (QListWidgetItem *item = phones_->currentItem()) {
			choice.phone_id = item->data(Qt::UserRole).toString().toStdString();
			choice.phone_name = item->data(Qt::UserRole + 1).toString().toStdString();
			choice.address.clear();
		}
		return true;
	}

private:
	void refresh()
	{
		const QString selected = phones_->currentItem() ? phones_->currentItem()->data(Qt::UserRole).toString()
								: QString();
		phones_->clear();
		for (const FoundPhone &phone : PhoneBrowser::instance().phones()) {
			auto item = new QListWidgetItem(
				QString::fromStdString(phone.name + " · " + phone.address + " · " + phone.version),
				phones_);
			item->setData(Qt::UserRole, QString::fromStdString(phone.id));
			item->setData(Qt::UserRole + 1, QString::fromStdString(phone.name));
			if (item->data(Qt::UserRole).toString() == selected)
				phones_->setCurrentItem(item);
		}
		emit completeChanged();
	}

	AddCameraWizard *wizard_;
	QListWidget *phones_;
	QRadioButton *manual_;
	QLineEdit *address_;
	std::unique_ptr<PhonesWatcher> watcher_;
};

class PicturePage final : public QWizardPage {
public:
	explicit PicturePage(AddCameraWizard *wizard) : wizard_(wizard)
	{
		setTitle(text("Wizard.Picture.Title"));
		setSubTitle(text("Wizard.Picture.Subtitle"));

		presets_ = new QComboBox(this);
		for (const StreamPreset &preset : stream_presets()) {
			const QString description = text(preset.description_key);
			presets_->addItem(QString::fromUtf8(preset.profile) + " · " + description, preset.id);
		}
		presets_->setToolTip(text("Camera.Preset.Tooltip"));

		looks_ = new QComboBox(this);
		for (const Look &look : all_looks()) {
			const QString name = look.builtin ? text(("Look." + look.id).c_str())
							  : QString::fromStdString(look.id);
			looks_->addItem(name, QString::fromStdString(look.id));
		}
		looks_->setCurrentIndex(looks_->findData(QStringLiteral("natural")));
		looks_->setToolTip(text("Dock.Look.Tooltip"));

		set_up_ = new QCheckBox(text("Wizard.Picture.SetUp"), this);
		set_up_->setChecked(true);
		set_up_->setToolTip(text("Dock.SetUp.Tooltip"));

		auto layout = new QFormLayout(this);
		layout->addRow(text("Camera.Preset"), presets_);
		layout->addRow(text("Camera.Look"), looks_);
		layout->addRow(set_up_);
		setFinalPage(true);
	}

	int nextId() const override { return -1; }

	bool validatePage() override
	{
		wizard_->choice.preset = presets_->currentData().toString().toStdString();
		wizard_->choice.look = looks_->currentData().toString().toStdString();
		wizard_->choice.set_up = set_up_->isChecked();
		return true;
	}

private:
	AddCameraWizard *wizard_;
	QComboBox *presets_;
	QComboBox *looks_;
	QCheckBox *set_up_;
};

std::string unique_source_name(const char *base)
{
	std::string name = base;
	for (int i = 2;; i++) {
		OBSSourceAutoRelease existing = obs_get_source_by_name(name.c_str());
		if (!existing)
			return name;
		name = std::string(base) + " " + std::to_string(i);
	}
}

} // namespace

AddCameraWizard::AddCameraWizard(QWidget *parent) : QWizard(parent)
{
	setWindowTitle(text("Wizard.Title"));
	setWizardStyle(QWizard::ModernStyle);
	setOption(QWizard::NoBackButtonOnStartPage);
	setButtonText(QWizard::FinishButton, text("Wizard.Add"));
	setPage(kPreparePage, new PreparePage(this));
	setPage(kChoosePage, new ChoosePage(this));
	setPage(kPicturePage, new PicturePage(this));
}

void AddCameraWizard::accept()
{
	// The scene being edited: the preview scene in Studio Mode, otherwise the current scene
	OBSSourceAutoRelease scene_source = obs_frontend_preview_program_mode_active()
						    ? obs_frontend_get_current_preview_scene()
						    : obs_frontend_get_current_scene();
	obs_scene_t *scene = obs_scene_from_source(scene_source);
	OBSWeakSource added;
	if (scene) {
		OBSDataAutoRelease settings = obs_data_create();
		obs_data_set_string(settings, "phone", choice.phone_id.empty() ? "manual" : choice.phone_id.c_str());
		obs_data_set_string(settings, "phone_name", choice.phone_name.c_str());
		obs_data_set_string(settings, "address", choice.address.c_str());
		obs_data_set_string(settings, "preset", choice.preset.c_str());
		obs_data_set_string(settings, "look", choice.look.c_str());
		obs_data_set_bool(settings, "set_up_pending", choice.set_up);

		const std::string name = unique_source_name(obs_module_text("Camera.Name"));
		OBSSourceAutoRelease camera = obs_source_create(kCameraSourceId, name.c_str(), settings, nullptr);
		obs_sceneitem_t *item = obs_scene_add(scene, camera);
		added = OBSGetWeakRef(camera);

		// Fitted to the canvas
		obs_video_info video = {};
		if (item && obs_get_video_info(&video)) {
			vec2 bounds;
			vec2_set(&bounds, static_cast<float>(video.base_width), static_cast<float>(video.base_height));
			obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_INNER);
			obs_sceneitem_set_bounds_alignment(item, OBS_ALIGN_CENTER);
			obs_sceneitem_set_bounds(item, &bounds);
		}
	}

	// The Camera Controls dock shows the new camera
	if (auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window())) {
		if (auto dock = main_window->findChild<QDockWidget *>(kControlsDockId)) {
			dock->show();
			dock->raise();
		}
	}
	OBSSourceAutoRelease camera = obs_weak_source_get_source(added);
	if (ControlsDock *controls = ControlsDock::instance(); controls && camera)
		controls->show_source(camera);
	QWizard::accept();
}

} // namespace bmagicam::ui
