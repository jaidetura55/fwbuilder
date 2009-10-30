/*

                          Firewall Builder

                 Copyright (C) 2006 NetCitadel, LLC

  Author:  Illiya Yalovoy <yalovoy@gmail.com>

  $Id$

  This program is free software which we release under the GNU General Public
  License. You may redistribute and/or modify this program under the terms
  of that license as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  To get a copy of the GNU General Public License, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#include "../../config.h"
#include "../../definitions.h"
#include "global.h"
#include "utils.h"
#include "platforms.h"

#include "instOptionsDialog.h"
#include "instConf.h"

#include "fwbuilder/Firewall.h"

#include <qstring.h>
#include <qlineedit.h>
#include <qcheckbox.h>
#include <qgroupbox.h>
#include <qlabel.h>
#include <qlayout.h>
#include <qspinbox.h>
#include <qframe.h>

#include <stdlib.h>

#include "FWBSettings.h"
#include "FWWindow.h"

using namespace std;
using namespace libfwbuilder;

instOptionsDialog::instOptionsDialog(QWidget *parent, instConf *_cnf) :
    QDialog(parent)
{
    m_dialog = new Ui::instOptionsDialog_q;
    m_dialog->setupUi(this);
    cnf = _cnf;

    Firewall *fw = Firewall::cast(cnf->fwobj);
    QString usrname = fw->getOptionsObject()->getStr("admUser").c_str();
    bool savePassEnabled = st->getBool("Environment/RememberSshPassEnabled");
    m_dialog->rememberPass->setEnabled( savePassEnabled );
    if (savePassEnabled)
    {
        m_dialog->rememberPass->setChecked( st->getBool("Environment/RememberSshPass") );
        QPair<QString, QString> passwds = mw->passwords[qMakePair(cnf->fwobj->getId(),
                                                                  usrname)];
        m_dialog->pwd->setText(passwds.first);
        m_dialog->epwd->setText(passwds.second);
    }
    else
        m_dialog->rememberPass->setChecked( false );

    m_dialog->pwd->setEchoMode( QLineEdit::Password );
    m_dialog->epwd->setEchoMode( QLineEdit::Password );

    m_dialog->uname->setText( cnf->user );
    m_dialog->incr->setChecked( cnf->incremental );
    m_dialog->test->setChecked( cnf->dry_run );
    m_dialog->backupConfigFile->setText( cnf->backup_file );
    m_dialog->saveDiff->setChecked( cnf->save_diff );
    m_dialog->saveStandby->setChecked( cnf->saveStandby );
    m_dialog->altAddress->setText( cnf->maddr );
    m_dialog->quiet->setChecked( cnf->quiet );
    m_dialog->verbose->setChecked( cnf->verbose );
    m_dialog->stripComments->setChecked( cnf->stripComments );
    m_dialog->compressScript->setChecked( cnf->compressScript );
    m_dialog->copyFWB->setChecked( cnf->copyFWB );
    m_dialog->testRun->setChecked( cnf->testRun );
    m_dialog->rollback->setChecked( cnf->rollback );
    m_dialog->rollbackTime->setValue( cnf->rollbackTime );
    m_dialog->cancelRollbackIfSuccess->setChecked( cnf->cancelRollbackIfSuccess );

    // If we have user name, bring focus to the password input field
    // if we do not have user name, focus goes to the user name field
    if (cnf->user.isEmpty()) m_dialog->uname->setFocus();
    else m_dialog->pwd->setFocus();


    if (cnf->batchInstall)
    {
        //m_dialog->copyFWB->hide();
        m_dialog->rollback->hide();
        m_dialog->rollbackTime->hide();
        m_dialog->rollbackTimeUnit->hide();
        m_dialog->cancelRollbackIfSuccess->hide();
        m_dialog->PIXgroupBox->hide();
        m_dialog->backupConfigFile->hide();
        m_dialog->backupConfigFileLbl->hide();
    } else
    {
        QString fwname = QString::fromUtf8(cnf->fwobj->getName().c_str());
        m_dialog->dialogTitleLine->setText(
            QString("<p align=\"center\"><b><font size=\"+2\">")+
            tr("Install options for firewall '%1'").arg(fwname)+
            QString("</font></b></p>")
        );

        QString platform = cnf->fwobj->getStr("platform").c_str();

        if (platform=="pix" || platform=="fwsm" || platform=="iosacl")
        {
            m_dialog->copyFWB->hide();
            // Hide elements of installOptions dialog for which we do not
            // have commands
            QString cmd = cnf->getCmdFromResource("schedule_rollback");
            // option "schedule_rollback" is currently used to control rollback
            // behavior only for pix, fwsm and ios
            if (cmd.isEmpty())
            {
                m_dialog->rollback->hide();
                m_dialog->rollbackTime->hide();
                m_dialog->rollbackTimeUnit->hide();
                m_dialog->cancelRollbackIfSuccess->hide();
            }

            if (platform=="iosacl") m_dialog->PIXgroupBox->hide();

        } else
        {
            m_dialog->epwd->hide();
            m_dialog->PIXgroupBox->hide();
            // cancelling rollback at the end of activation is currently
            // only supported on pix,fwsm and ios
            m_dialog->cancelRollbackIfSuccess->hide();
        }
    }

/* hide anyway, diff does not work for pix 6.3(3) */
    m_dialog->saveDiff->hide();

    m_dialog->stripComments->hide();

    m_dialog->compressScript->hide();

    m_dialog->PIXgroupBox->adjustSize();
    m_dialog->generalOptionsBox->adjustSize();
    m_dialog->testOptionsBox->adjustSize();
    m_dialog->mainBox->adjustSize();

    adjustSize();

    if (fwbdebug)
    {
        QSize sz = sizeHint();
        qDebug(QString("instOptionsDialog:  sizeHint: %1x%2").arg(sz.width()).arg(sz.height()).toAscii().constData());
        sz = minimumSizeHint();
        qDebug(QString("instOptionsDialog:  minimumSizeHint: %1x%2").
                arg(sz.width()).arg(sz.height()).toAscii().constData());

        QRect bfr;

        bfr = m_dialog->titleFrame->geometry();
        qDebug(QString("instOptionsDialog:  titleFrame: top=%1 bottom=%2").
                arg(bfr.top()).arg(bfr.bottom()).toAscii().constData());
        bfr = m_dialog->buttonsFrame->geometry();
        qDebug(QString("instOptionsDialog:  buttonsFrame: top=%1 bottom=%2").
                arg(bfr.top()).arg(bfr.bottom()).toAscii().constData());
    }

    //resize( minimumSizeHint() );

    //adjustSize();

    //dlg->setFixedHeight( dlg->minimumSizeHint().height() );
}

void instOptionsDialog::savePassword()
{
    Firewall *fw = Firewall::cast(cnf->fwobj);
    fw->getOptionsObject()->setStr("admUser", m_dialog->uname->text().toStdString());
    if ( m_dialog->rememberPass->isChecked() )
        mw->passwords[qMakePair(cnf->fwobj->getId(), m_dialog->uname->text())] =
                 qMakePair(m_dialog->pwd->text(), m_dialog->epwd->text());
    else
        mw->passwords.remove(qMakePair(cnf->fwobj->getId(), m_dialog->uname->text()));
    st->setBool("Environment/RememberSshPass", m_dialog->rememberPass->isChecked());
}

instOptionsDialog::~instOptionsDialog()
{
    delete m_dialog;
}


QString instOptionsDialog::getUName() { return m_dialog->uname->text(); }
QString instOptionsDialog::getPWD()   { return m_dialog->pwd->text();   }
QString instOptionsDialog::getEPWD()  { return m_dialog->epwd->text();  }

