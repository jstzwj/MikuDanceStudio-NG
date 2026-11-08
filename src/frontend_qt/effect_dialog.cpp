#include "effect_dialog.hpp"
#include "editor_window.hpp"
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QTableWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTimer>
#include <cstring>
#include <cmath>
#include <sstream>
#include <iomanip>
namespace mikudancestudio::qt {
namespace {
QString ValueText(const mme::ParameterValue& value) {
    QStringList words;
    for (auto word : value.words) {
        if (value.type == mme::ParameterValue::Type::Float) {
            float f;
            std::memcpy(&f, &word, 4);
            words << QString::number(f, 'g', 9);
        } else if (value.type == mme::ParameterValue::Type::Int) {
            std::int32_t n;
            std::memcpy(&n, &word, 4);
            words << QString::number(n);
        } else
            words << (word ? "true" : "false");
    }
    return words.join(' ');
}
bool ParseValue(QString text, mme::ParameterValue& value) {
    text.replace(',', ' ');
    std::istringstream input(text.toStdString());
    input.imbue(std::locale::classic());
    for (auto& word : value.words) {
        if (value.type == mme::ParameterValue::Type::Float) {
            float f;
            if (!(input >> f) || !std::isfinite(f))
                return false;
            std::memcpy(&word, &f, 4);
        } else if (value.type == mme::ParameterValue::Type::Int) {
            std::int32_t n;
            if (!(input >> n))
                return false;
            std::memcpy(&word, &n, 4);
        } else {
            std::string s;
            if (!(input >> s))
                return false;
            if (s == "true" || s == "1")
                word = 1;
            else if (s == "false" || s == "0")
                word = 0;
            else
                return false;
        }
    }
    input >> std::ws;
    return input.eof();
}
}
EffectDialog::EffectDialog(editor::EditorSession& session, Language& language, Inspect inspect,
                           std::function<std::string()> report, std::function<void()> changed,
                           QWidget* parent)
    : QDialog(parent), session_(session), language_(language), inspect_(std::move(inspect)),
      report_(std::move(report)), changed_(std::move(changed)) {
    setObjectName("mme.effects");
    resize(680, 560);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
#ifdef __ANDROID__
    setWindowFlags(Qt::Window);
    setWindowState(Qt::WindowMaximized);
#endif
    auto* layout = new QVBoxLayout(this);
    auto* row = new QHBoxLayout;
    objects_ = new QComboBox(this);
    objects_->setObjectName("mme.object");
    objects_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    subsets_ = new QComboBox(this);
    subsets_->setObjectName("mme.subset");
    row->addWidget(objects_, 2);
    row->addWidget(subsets_, 1);
    layout->addLayout(row);
    row = new QHBoxLayout;
    path_ = new QLineEdit(this);
    path_->setObjectName("mme.path");
    path_->setReadOnly(true);
    row->addWidget(path_, 1);
    const auto button = [&](const char* id) {
        auto* b = new QPushButton(this);
        b->setObjectName(id);
        row->addWidget(b);
        return b;
    };
    browse_ = button("mme.assign");
    remove_ = button("mme.remove");
    reload_ = button("mme.reload");
    layout->addLayout(row);
    row = new QHBoxLayout;
    enabled_ = new QCheckBox(this);
    enabled_->setObjectName("mme.enabled");
    basic_ = new QCheckBox(this);
    basic_->setObjectName("mme.basic");
    row->addWidget(enabled_);
    row->addWidget(basic_);
    row->addStretch();
    undo_ = button("mme.undo");
    redo_ = button("mme.redo");
    layout->addLayout(row);
    parameters_ = new QTableWidget(0, 3, this);
    parameters_->setObjectName("mme.parameters");
    parameters_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    parameters_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    layout->addWidget(parameters_, 2);
    reportBox_ = new QPlainTextEdit(this);
    reportBox_->setObjectName("mme.report");
    reportBox_->setReadOnly(true);
    layout->addWidget(reportBox_, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::hide);
    connect(objects_, &QComboBox::currentIndexChanged, this, [this] {
        if (!refreshing_)
            ObjectChanged();
    });
    connect(subsets_, &QComboBox::currentIndexChanged, this, [this] {
        if (!refreshing_)
            SelectionChanged();
    });
    connect(browse_, &QPushButton::clicked, this, [this] {
        const auto file = QFileDialog::getOpenFileName(this, language_.Text("mme.assign"),
                                                       path_->text(), "MME (*.fx)");
        if (file.isEmpty())
            return;
        std::string r;
        Apply(session_.AssignEffect(Address(), Path(file), r), r);
    });
    connect(remove_, &QPushButton::clicked, this, [this] {
        std::string r;
        Apply(session_.AssignEffect(Address(), {}, r), r);
    });
    connect(reload_, &QPushButton::clicked, this, [this] {
        std::string r;
        Apply(session_.ReloadEffect(Address(), r), r);
    });
    connect(enabled_, &QCheckBox::toggled, this, [this](bool v) {
        if (refreshing_)
            return;
        std::string r;
        Apply(session_.EnableEffect(Address(), v, r), r);
    });
    connect(basic_, &QCheckBox::toggled, this, [this](bool v) {
        if (refreshing_)
            return;
        session_.SetBasicMaterials(v);
        Apply(true, {});
    });
    connect(undo_, &QPushButton::clicked, this, [this] {
        std::string r;
        Apply(session_.UndoEffects(r), r);
    });
    connect(redo_, &QPushButton::clicked, this, [this] {
        std::string r;
        Apply(session_.RedoEffects(r), r);
    });
    connect(parameters_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (refreshing_ || item->column() != 2)
            return;
        const auto name = parameters_->item(item->row(), 0)->text().toStdString();
        auto value = values_.at(name);
        std::string r;
        if (!ParseValue(item->text(), value)) {
            Apply(false, "effect-parameter-value");
            return;
        }
        Apply(session_.SetEffectParameter(Address(), name, value, r), r);
    });
    auto* timer = new QTimer(this);
    timer->setInterval(1000);
    connect(timer, &QTimer::timeout, this, [this] {
        if (isVisible())
            Refresh();
    });
    timer->start();
    Retranslate();
    Refresh();
}
editor::EffectAddress EffectDialog::Address() const {
    const auto value = objects_->currentData().toUInt();
    return {bool(value & 256), std::uint8_t(value & 255), subsets_->currentData().toInt()};
}
void EffectDialog::Apply(bool ok, const std::string& reason) {
    if (ok && changed_)
        changed_();
    revision_ = 0;
    Refresh();
    if (!ok)
        reportBox_->setPlainText(QString::fromStdString(reason));
}
void EffectDialog::Refresh() {
    if (refreshing_)
        return;
    reportBox_->setPlainText(QString::fromStdString(report_()));
    if (revision_ == session_.Revision())
        return;
    refreshing_ = true;
    const auto selected = objects_->currentData();
    objects_->clear();
    for (const auto& [slot, path] : session_.Assets().modelPaths)
        objects_->addItem(Path(path.filename()), int(slot));
    for (const auto& [slot, path] : session_.Assets().accessoryPaths)
        objects_->addItem(Path(path.filename()), int(slot) | 256);
    objects_->setCurrentIndex(std::max(0, objects_->findData(selected)));
    refreshing_ = false;
    ObjectChanged();
    revision_ = session_.Revision();
}
void EffectDialog::ObjectChanged() {
    refreshing_ = true;
    const auto previous = subsets_->currentData();
    subsets_->clear();
    subsets_->addItem(language_.Text("mme.whole"), -1);
    const auto a = Address();
    unsigned count = 0;
    if (objects_->count()) {
        if (a.accessory) {
            const auto it = session_.Assets().accessories.find(a.slot);
            if (it != session_.Assets().accessories.end())
                count = unsigned(it->second->Subsets().size());
        } else {
            const auto it = session_.Assets().models.find(a.slot);
            if (it != session_.Assets().models.end())
                count = unsigned(it->second->Document().materials.size());
        }
    }
    for (unsigned n = 0; n < count; ++n)
        subsets_->addItem(QString::number(n), int(n));
    subsets_->setCurrentIndex(std::max(0, subsets_->findData(previous)));
    refreshing_ = false;
    SelectionChanged();
}
void EffectDialog::SelectionChanged() {
    refreshing_ = true;
    const auto address = Address();
    const auto it = session_.Effects().assignments.find(address);
    const bool assigned = objects_->count() && it != session_.Effects().assignments.end();
    path_->setText(assigned ? Path(it->second.source) : QString());
    enabled_->setEnabled(assigned);
    enabled_->setChecked(assigned && it->second.enabled);
    basic_->setChecked(session_.Effects().basicMaterials);
    browse_->setEnabled(objects_->count());
    remove_->setEnabled(assigned);
    reload_->setEnabled(assigned);
    undo_->setEnabled(session_.CanUndoEffects());
    redo_->setEnabled(session_.CanRedoEffects());
    parameters_->setRowCount(0);
    values_.clear();
    const auto def = assigned ? inspect_(address) : nullptr;
    std::string reason;
    if (def && mme::EvaluateDefaults(*def, values_, reason)) {
        for (const auto& v : def->metadata.variables) {
            auto found = values_.find(v.name);
            if (found == values_.end() || found->second.type == mme::ParameterValue::Type::String)
                continue;
            if (assigned) {
                const auto override = it->second.parameters.find(v.name);
                if (override != it->second.parameters.end())
                    found->second = override->second;
            }
            const int row = parameters_->rowCount();
            parameters_->insertRow(row);
            for (int col = 0; col < 3; ++col) {
                auto* item = new QTableWidgetItem(col == 0   ? QString::fromStdString(v.name)
                                                  : col == 1 ? QString::fromStdString(v.type)
                                                             : ValueText(found->second));
                if (col != 2 || v.immutable || !v.semantic.empty())
                    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                parameters_->setItem(row, col, item);
            }
        }
    }
    refreshing_ = false;
}
void EffectDialog::Retranslate() {
    setWindowTitle(language_.Text("mme.title"));
    for (auto* button : {browse_, remove_, reload_, undo_, redo_})
        button->setText(language_.Text(button->objectName()));
    enabled_->setText(language_.Text("mme.enabled"));
    basic_->setText(language_.Text("mme.basic"));
    basic_->setToolTip(language_.Text("mme.basic.help"));
    parameters_->setHorizontalHeaderLabels(
        {language_.Text("mme.parameter"), language_.Text("mme.type"), language_.Text("mme.value")});
    revision_ = 0;
    Refresh();
}
}
