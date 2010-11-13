/*
 * Bittorrent Client using Qt4 and libtorrent.
 * Copyright (C) 2010  Christophe Dumez
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * In addition, as a special exception, the copyright holders give permission to
 * link this program with the OpenSSL project's "OpenSSL" library (or with
 * modified versions of it that use the same license as the "OpenSSL" library),
 * and distribute the linked executables. You must obey the GNU General Public
 * License in all respects for all of the code used other than "OpenSSL".  If you
 * modify file(s), you may extend this exception to your version of the file(s),
 * but you are not obligated to do so. If you do not wish to do so, delete this
 * exception statement from your version.
 *
 * Contact : chris@qbittorrent.org
 */

#include <QInputDialog>
#include <QMessageBox>
#include <QFileDialog>
#include <QDebug>
#include <QMenu>
#include <QCursor>

#include "automatedrssdownloader.h"
#include "ui_automatedrssdownloader.h"
#include "rsssettings.h"
#include "rssdownloadrulelist.h"
#include "preferences.h"
#include "qinisettings.h"

AutomatedRssDownloader::AutomatedRssDownloader(QWidget *parent) :
  QDialog(parent),
  ui(new Ui::AutomatedRssDownloader)
{
  ui->setupUi(this);
  ui->listRules->setSortingEnabled(true);
  ui->listRules->setSelectionMode(QAbstractItemView::ExtendedSelection);
  connect(ui->listRules, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(displayRulesListMenu(const QPoint&)));
  m_ruleList = RssDownloadRuleList::instance();
  initLabelCombobox();
  loadFeedList();
  loadSettings();
  connect(ui->listRules, SIGNAL(currentItemChanged(QListWidgetItem*,QListWidgetItem*)), SLOT(handleCurrentItemChange(QListWidgetItem*,QListWidgetItem*)));
  connect(ui->listRules, SIGNAL(itemSelectionChanged()), SLOT(updateRuleDefinitionBox()));
  connect(ui->listRules, SIGNAL(itemSelectionChanged()), SLOT(updateFeedList()));
  connect(ui->listFeeds, SIGNAL(itemChanged(QListWidgetItem*)), SLOT(handleFeedCheckStateChange(QListWidgetItem*)));
  updateRuleDefinitionBox();
  updateFeedList();
}

AutomatedRssDownloader::~AutomatedRssDownloader()
{
  qDebug() << Q_FUNC_INFO;
  // Save current item on exit
  saveCurrentRule(ui->listRules->currentItem());
  saveSettings();
  delete ui;
}

void AutomatedRssDownloader::loadSettings()
{
  // load dialog geometry
  QIniSettings settings("qBittorrent", "qBittorrent");
  restoreGeometry(settings.value("RssFeedDownloader/geometry").toByteArray());
  ui->checkEnableDownloader->setChecked(RssSettings::isRssDownloadingEnabled());
  // Display download rules
  loadRulesList();
}

void AutomatedRssDownloader::saveSettings()
{
  RssSettings::setRssDownloadingEnabled(ui->checkEnableDownloader->isChecked());
  // Save dialog geometry
  QIniSettings settings("qBittorrent", "qBittorrent");
  settings.setValue("RssFeedDownloader/geometry", saveGeometry());
}

void AutomatedRssDownloader::loadRulesList()
{
  // Make sure we save the current item before clearing
  if(ui->listRules->currentItem()) {
    saveCurrentRule(ui->listRules->currentItem());
  }
  ui->listRules->clear();
  foreach (const QString &rule_name, m_ruleList->ruleNames()) {
    QListWidgetItem *item = new QListWidgetItem(rule_name, ui->listRules);
    item->setFlags(item->flags()|Qt::ItemIsUserCheckable);
    if(m_ruleList->getRule(rule_name).isEnabled())
      item->setCheckState(Qt::Checked);
    else
      item->setCheckState(Qt::Unchecked);
  }
  if(ui->listRules->count() > 0 && !ui->listRules->currentItem())
    ui->listRules->setCurrentRow(0);
}

void AutomatedRssDownloader::loadFeedList()
{
  const QStringList feed_aliases = RssSettings::getRssFeedsAliases();
  const QStringList feed_urls = RssSettings::getRssFeedsUrls();
  for(int i=0; i<feed_aliases.size(); ++i) {
    QListWidgetItem *item = new QListWidgetItem(feed_aliases.at(i), ui->listFeeds);
    item->setData(Qt::UserRole, feed_urls.at(i));
    item->setFlags(item->flags()|Qt::ItemIsUserCheckable);
  }
}

QStringList AutomatedRssDownloader::getSelectedFeeds() const
{
  QStringList feeds;
  for(int i=0; i<ui->listFeeds->count(); ++i) {
    QListWidgetItem *item = ui->listFeeds->item(i);
    if(item->checkState() != Qt::Unchecked)
      feeds << item->data(Qt::UserRole).toString();
  }
  return feeds;
}

void AutomatedRssDownloader::updateFeedList()
{
  for(int i=0; i<ui->listFeeds->count(); ++i) {
    QListWidgetItem *item = ui->listFeeds->item(i);
    const QString feed_url = item->data(Qt::UserRole).toString();
    bool all_enabled = false;
    foreach(QListWidgetItem *ruleItem, ui->listRules->selectedItems()) {
       RssDownloadRule rule = m_ruleList->getRule(ruleItem->text());
       if(!rule.isValid()) continue;
       if(rule.rssFeeds().contains(feed_url)) {
         all_enabled = true;
       } else {
         all_enabled = false;
         break;
       }
    }
    if(all_enabled)
      item->setCheckState(Qt::Checked);
    else
      item->setCheckState(Qt::Unchecked);
  }
  ui->listFeeds->setEnabled(!ui->listRules->selectedItems().isEmpty());
}

bool AutomatedRssDownloader::isRssDownloaderEnabled() const
{
  return ui->checkEnableDownloader->isChecked();
}

void AutomatedRssDownloader::updateRuleDefinitionBox()
{
  qDebug() << Q_FUNC_INFO;
  // Update rule definition box
  const QList<QListWidgetItem*> selection = ui->listRules->selectedItems();
  if(selection.count() == 1) {
    RssDownloadRule rule = getCurrentRule();
    if(rule.isValid()) {
      ui->lineContains->setText(rule.mustContain());
      ui->lineNotContains->setText(rule.mustNotContain());
      ui->saveDiffDir_check->setChecked(!rule.savePath().isEmpty());
      ui->lineSavePath->setText(rule.savePath());
      if(rule.label().isEmpty()) {
        ui->comboLabel->setCurrentIndex(-1);
        ui->comboLabel->clearEditText();
      } else {
        ui->comboLabel->setCurrentIndex(ui->comboLabel->findText(rule.label()));
      }
    } else {
      // New rule
      clearRuleDefinitionBox();
      ui->lineContains->setText(selection.first()->text());
    }
    // Enable
    ui->ruleDefBox->setEnabled(true);
  } else {
    // Clear
    clearRuleDefinitionBox();
    ui->ruleDefBox->setEnabled(false);
  }
}

void AutomatedRssDownloader::clearRuleDefinitionBox()
{
  ui->lineContains->clear();
  ui->lineNotContains->clear();
  ui->saveDiffDir_check->setChecked(false);
  ui->lineSavePath->clear();
  ui->comboLabel->clearEditText();
}

RssDownloadRule AutomatedRssDownloader::getCurrentRule() const
{
  QListWidgetItem * current_item = ui->listRules->currentItem();
  if(current_item)
    return m_ruleList->getRule(current_item->text());
  return RssDownloadRule();
}

void AutomatedRssDownloader::initLabelCombobox()
{
  // Load custom labels
  const QStringList customLabels = Preferences::getTorrentLabels();
  foreach(const QString& label, customLabels) {
    ui->comboLabel->addItem(label);
  }
}

void AutomatedRssDownloader::handleCurrentItemChange(QListWidgetItem *current, QListWidgetItem *previous)
{
  Q_UNUSED(current);
  saveCurrentRule(previous);
}

void AutomatedRssDownloader::saveCurrentRule(QListWidgetItem * item)
{
  qDebug() << Q_FUNC_INFO << item;
  if(!item) return;
  if(!ui->listRules->findItems(item->text(), Qt::MatchExactly).isEmpty()) return;
  RssDownloadRule rule = m_ruleList->getRule(item->text());
  if(!rule.isValid()) {
    rule.setName(item->text());
  }
  if(item->checkState() == Qt::Unchecked)
    rule.setEnabled(false);
  else
    rule.setEnabled(true);
  rule.setMustContain(ui->lineContains->text());
  rule.setMustNotContain(ui->lineNotContains->text());
  if(ui->saveDiffDir_check->isChecked())
    rule.setSavePath(ui->lineSavePath->text());
  else
    rule.setSavePath("");
  rule.setLabel(ui->comboLabel->currentText());
  // Save new label
  if(!rule.label().isEmpty())
    Preferences::addTorrentLabel(rule.label());
  rule.setRssFeeds(getSelectedFeeds());
  // Save it
  m_ruleList->saveRule(rule);
}


void AutomatedRssDownloader::on_addRuleBtn_clicked()
{
  // Ask for a rule name
  const QString rule = QInputDialog::getText(this, tr("New rule name"), tr("Please type the name of the new download rule."));
  if(rule.isEmpty()) return;
  // Check if this rule name already exists
  if(m_ruleList->getRule(rule).isValid()) {
    QMessageBox::warning(this, tr("Rule name conflict"), tr("A rule with this name already exists, please choose another name."));
    return;
  }
  // Add the new rule to the list
  QListWidgetItem * item = new QListWidgetItem(rule, ui->listRules);
  item->setFlags(item->flags()|Qt::ItemIsUserCheckable);
  item->setCheckState(Qt::Checked); // Enable as a default
  ui->listRules->setCurrentItem(item);
}

void AutomatedRssDownloader::on_removeRuleBtn_clicked()
{
  const QList<QListWidgetItem*> selection = ui->listRules->selectedItems();
  if(selection.isEmpty()) return;
  // Ask for confirmation
  QString confirm_text;
  if(selection.count() == 1)
    confirm_text = tr("Are you sure you want to remove the download rule named %1?").arg(selection.first()->text());
  else
    confirm_text = tr("Are you sure you want to remove the selected download rules?");
  if(QMessageBox::question(this, tr("Rule deletion confirmation"), confirm_text, QMessageBox::Yes, QMessageBox::No) != QMessageBox::Yes)
    return;
  foreach(QListWidgetItem *item, selection) {
    // Actually remove the item
    ui->listRules->removeItemWidget(item);
    // Remove it from the m_ruleList
    m_ruleList->removeRule(item->text());
    // Clean up memory
    delete item;
  }
}

void AutomatedRssDownloader::on_browseSP_clicked()
{
  QString save_path = QFileDialog::getExistingDirectory(this, tr("Destination directory"), QDir::homePath());
  if(!save_path.isEmpty())
    ui->lineSavePath->setText(save_path);
}

void AutomatedRssDownloader::on_exportBtn_clicked()
{
  if(m_ruleList->isEmpty()) {
    QMessageBox::warning(this, tr("Invalid action"), tr("The list is empty, there is nothing to export."));
    return;
  }
  // Ask for a save path
  QString save_path = QFileDialog::getSaveFileName(this, tr("Where would you like to save the list?"), QDir::homePath(), tr("Rules list (*.rssrules)"));
  if(save_path.isEmpty()) return;
  if(!save_path.endsWith(".rssrules", Qt::CaseInsensitive))
    save_path += ".rssrules";
  if(!m_ruleList->serialize(save_path)) {
    QMessageBox::warning(this, tr("I/O Error"), tr("Failed to create the destination file"));
    return;
  }
}

void AutomatedRssDownloader::on_importBtn_clicked()
{
  // Ask for filter path
  QString load_path = QFileDialog::getOpenFileName(this, tr("Please point to the RSS download rules file"), QDir::homePath(), tr("Rules list (*.rssrules *.filters)"));
  if(load_path.isEmpty() || !QFile::exists(load_path)) return;
  // Load it
  if(!m_ruleList->unserialize(load_path)) {
    QMessageBox::warning(this, tr("Import Error"), tr("Failed to import the selected rules file"));
    return;
  }
  // Reload the rule list
  loadRulesList();
}

void AutomatedRssDownloader::displayRulesListMenu(const QPoint &pos)
{
  Q_UNUSED(pos);
  QMenu menu;
  QAction *addAct = menu.addAction(QIcon(":/Icons/oxygen/list-add.png"), tr("Add new rule..."));
  QAction *delAct = 0;
  QAction *renameAct = 0;
  const QList<QListWidgetItem*> selection = ui->listRules->selectedItems();
  if(!selection.isEmpty()) {
    if(selection.count() == 1) {
      delAct = menu.addAction(QIcon(":/Icons/oxygen/list-remove.png"), tr("Delete rule"));
      menu.addSeparator();
      renameAct = menu.addAction(QIcon(":/Icons/oxygen/edit_clear.png"), tr("Rename rule..."));
    } else {
      delAct = menu.addAction(QIcon(":/Icons/oxygen/list-remove.png"), tr("Delete selected rules"));
    }
  }
  QAction *act = menu.exec(QCursor::pos());
  if(!act) return;
  if(act == addAct) {
    on_addRuleBtn_clicked();
    return;
  }
  if(act == delAct) {
    on_removeRuleBtn_clicked();
    return;
  }
  if(act == renameAct) {
    renameSelectedRule();
    return;
  }
}

void AutomatedRssDownloader::renameSelectedRule()
{
  QListWidgetItem *item = ui->listRules->currentItem();
  if(!item) return;
  forever {
    QString new_name = QInputDialog::getText(this, tr("Rule renaming"), tr("Please type the new rule name"), QLineEdit::Normal, item->text());
    new_name = new_name.trimmed();
    if(new_name.isEmpty()) return;
    if(m_ruleList->ruleNames().contains(new_name, Qt::CaseInsensitive)) {
      QMessageBox::warning(this, tr("Rule name conflict"), tr("A rule with this name already exists, please choose another name."));
    } else {
      // Rename the rule
      m_ruleList->renameRule(item->text(), new_name);
      item->setText(new_name);
      return;
    }
  }
}

void AutomatedRssDownloader::handleFeedCheckStateChange(QListWidgetItem *feed_item)
{
  if(ui->ruleDefBox->isEnabled()) {
    // Make sure the current rule is saved
    saveCurrentRule(ui->listRules->currentItem());
  }
  foreach (QListWidgetItem* rule_item, ui->listRules->selectedItems()) {
    RssDownloadRule rule = m_ruleList->getRule(rule_item->text());
    // TODO
  }
}

