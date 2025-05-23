/******************************************************************************
 *   Copyright (c) 2012 Jan Rheinländer <jrheinlaender@users.sourceforge.net> *
 *                                                                            *
 *   This file is part of the FreeCAD CAx development system.                 *
 *                                                                            *
 *   This library is free software; you can redistribute it and/or            *
 *   modify it under the terms of the GNU Library General Public              *
 *   License as published by the Free Software Foundation; either             *
 *   version 2 of the License, or (at your option) any later version.         *
 *                                                                            *
 *   This library  is distributed in the hope that it will be useful,         *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 *   GNU Library General Public License for more details.                     *
 *                                                                            *
 *   You should have received a copy of the GNU Library General Public        *
 *   License along with this library; see the file COPYING.LIB. If not,       *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,            *
 *   Suite 330, Boston, MA  02111-1307, USA                                   *
 *                                                                            *
 ******************************************************************************/


#include "PreCompiled.h"

#ifndef _PreComp_
#include <QMessageBox>
#include <QTimer>
#endif

#include <Base/UnitsApi.h>
#include <App/Application.h>
#include <App/Document.h>
#include <App/Origin.h>
#include <App/Datums.h>
#include <Gui/Application.h>
#include <Gui/Document.h>
#include <Gui/BitmapFactory.h>
#include <Gui/ViewProvider.h>
#include <Gui/WaitCursor.h>
#include <Base/Console.h>
#include <Gui/Selection/Selection.h>
#include <Gui/Command.h>
#include <Gui/ViewProviderCoordinateSystem.h>
#include <Mod/PartDesign/App/FeaturePolarPattern.h>
#include <Mod/Sketcher/App/SketchObject.h>
#include <Mod/PartDesign/App/DatumLine.h>
#include <Mod/PartDesign/App/Body.h>

#include "ReferenceSelection.h"
#include "TaskMultiTransformParameters.h"
#include "Utils.h"

#include "ui_TaskPolarPatternParameters.h"
#include "TaskPolarPatternParameters.h"

using namespace PartDesignGui;
using namespace Gui;

/* TRANSLATOR PartDesignGui::TaskPolarPatternParameters */

TaskPolarPatternParameters::TaskPolarPatternParameters(ViewProviderTransformed* TransformedView,
                                                       QWidget* parent)
    : TaskTransformedParameters(TransformedView, parent)
    , ui(new Ui_TaskPolarPatternParameters)
{
    setupUI();
}

TaskPolarPatternParameters::TaskPolarPatternParameters(TaskMultiTransformParameters* parentTask,
                                                       QWidget* parameterWidget)
    : TaskTransformedParameters(parentTask)
    , ui(new Ui_TaskPolarPatternParameters)
{
    setupParameterUI(parameterWidget);
}

void TaskPolarPatternParameters::setupParameterUI(QWidget* widget)
{
    ui->setupUi(widget);
    QMetaObject::connectSlotsByName(this);

    // Get the feature data
    auto pcPolarPattern = getObject<PartDesign::PolarPattern>();

    ui->polarAngle->bind(pcPolarPattern->Angle);
    ui->angleOffset->bind(pcPolarPattern->Offset);

    ui->spinOccurrences->bind(pcPolarPattern->Occurrences);
    ui->spinOccurrences->setMaximum(pcPolarPattern->Occurrences.getMaximum());
    ui->spinOccurrences->setMinimum(pcPolarPattern->Occurrences.getMinimum());

    ui->comboAxis->setEnabled(true);
    ui->comboMode->setEnabled(true);
    ui->checkReverse->setEnabled(true);
    ui->polarAngle->setEnabled(true);
    ui->spinOccurrences->setEnabled(true);

    this->axesLinks.setCombo(*(ui->comboAxis));
    App::DocumentObject* sketch = getSketchObject();
    if (sketch && sketch->isDerivedFrom<Part::Part2DObject>()) {
        this->fillAxisCombo(axesLinks, static_cast<Part::Part2DObject*>(sketch));
    }
    else {
        this->fillAxisCombo(axesLinks, nullptr);
    }

    // show the parts coordinate system axis for selection
    PartDesign::Body* body = PartDesign::Body::findBodyOf(getObject());

    if (body) {
        try {
            App::Origin* origin = body->getOrigin();
            auto vpOrigin = static_cast<ViewProviderCoordinateSystem*>(
                Gui::Application::Instance->getViewProvider(origin));
            vpOrigin->setTemporaryVisibility(Gui::DatumElement::Axes);
        }
        catch (const Base::Exception& ex) {
            Base::Console().error("%s\n", ex.what());
        }
    }

    adaptVisibilityToMode();
    updateUI();

    updateViewTimer = new QTimer(this);
    updateViewTimer->setSingleShot(true);
    updateViewTimer->setInterval(getUpdateViewTimeout());
    connect(updateViewTimer,
            &QTimer::timeout,
            this,
            &TaskPolarPatternParameters::onUpdateViewTimer);
    connect(ui->comboAxis,
            qOverload<int>(&QComboBox::activated),
            this,
            &TaskPolarPatternParameters::onAxisChanged);
    connect(ui->comboMode,
            qOverload<int>(&QComboBox::activated),
            this,
            &TaskPolarPatternParameters::onModeChanged);
    connect(ui->checkReverse,
            &QCheckBox::toggled,
            this,
            &TaskPolarPatternParameters::onCheckReverse);
    connect(ui->polarAngle,
            qOverload<double>(&Gui::QuantitySpinBox::valueChanged),
            this,
            &TaskPolarPatternParameters::onAngle);
    connect(ui->angleOffset,
            qOverload<double>(&Gui::QuantitySpinBox::valueChanged),
            this,
            &TaskPolarPatternParameters::onOffset);
    connect(ui->spinOccurrences,
            &Gui::UIntSpinBox::unsignedChanged,
            this,
            &TaskPolarPatternParameters::onOccurrences);
}

void TaskPolarPatternParameters::retranslateParameterUI(QWidget* widget)
{
    ui->retranslateUi(widget);
}

void TaskPolarPatternParameters::updateUI()
{
    if (blockUpdate) {
        return;
    }
    blockUpdate = true;

    auto pcPolarPattern = getObject<PartDesign::PolarPattern>();

    auto mode = static_cast<PartDesign::PolarPatternMode>(pcPolarPattern->Mode.getValue());
    bool reverse = pcPolarPattern->Reversed.getValue();
    double angle = pcPolarPattern->Angle.getValue();
    double offset = pcPolarPattern->Offset.getValue();
    unsigned occurrences = pcPolarPattern->Occurrences.getValue();

    if (axesLinks.setCurrentLink(pcPolarPattern->Axis) == -1) {
        // failed to set current, because the link isn't in the list yet
        axesLinks.addLink(
            pcPolarPattern->Axis,
            getRefStr(pcPolarPattern->Axis.getValue(), pcPolarPattern->Axis.getSubValues()));
        axesLinks.setCurrentLink(pcPolarPattern->Axis);
    }

    // Note: This block of code would trigger change signal handlers (e.g. onOccurrences())
    // and another updateUI() if we didn't check for blockUpdate
    ui->checkReverse->setChecked(reverse);
    ui->comboMode->setCurrentIndex(static_cast<int>(mode));
    ui->polarAngle->setValue(angle);
    ui->angleOffset->setValue(offset);
    ui->spinOccurrences->setValue(occurrences);

    blockUpdate = false;
}

void TaskPolarPatternParameters::onUpdateViewTimer()
{
    setupTransaction();
    recomputeFeature();
}

void TaskPolarPatternParameters::kickUpdateViewTimer() const
{
    updateViewTimer->start();
}

void TaskPolarPatternParameters::adaptVisibilityToMode()
{
    auto pcLinearPattern = getObject<PartDesign::PolarPattern>();
    auto mode = static_cast<PartDesign::PolarPatternMode>(pcLinearPattern->Mode.getValue());

    ui->polarAngleWrapper->setVisible(mode == PartDesign::PolarPatternMode::angle);
    ui->angleOffsetWrapper->setVisible(mode == PartDesign::PolarPatternMode::offset);
}

void TaskPolarPatternParameters::onSelectionChanged(const Gui::SelectionChanges& msg)
{
    if (selectionMode != SelectionMode::None && msg.Type == Gui::SelectionChanges::AddSelection) {
        if (originalSelected(msg)) {
            exitSelectionMode();
        }
        else {
            auto pcPolarPattern = getObject<PartDesign::PolarPattern>();

            std::vector<std::string> axes;
            App::DocumentObject* selObj = nullptr;
            getReferencedSelection(pcPolarPattern, msg, selObj, axes);
            if (!selObj) {
                return;
            }

            if (selectionMode == SelectionMode::Reference || selObj->isDerivedFrom<App::Line>()) {
                setupTransaction();
                pcPolarPattern->Axis.setValue(selObj, axes);
                recomputeFeature();
                updateUI();
            }
            exitSelectionMode();
        }
    }
}

void TaskPolarPatternParameters::onCheckReverse(const bool on)
{
    if (blockUpdate) {
        return;
    }
    auto pcPolarPattern = getObject<PartDesign::PolarPattern>();
    pcPolarPattern->Reversed.setValue(on);

    exitSelectionMode();
    kickUpdateViewTimer();
}

void TaskPolarPatternParameters::onModeChanged(const int mode)
{
    if (blockUpdate) {
        return;
    }
    auto pcPolarPattern = getObject<PartDesign::PolarPattern>();
    pcPolarPattern->Mode.setValue(mode);

    adaptVisibilityToMode();

    exitSelectionMode();
    kickUpdateViewTimer();
}

void TaskPolarPatternParameters::onAngle(const double angle)
{
    if (blockUpdate) {
        return;
    }
    auto pcPolarPattern = getObject<PartDesign::PolarPattern>();
    pcPolarPattern->Angle.setValue(angle);

    exitSelectionMode();
    kickUpdateViewTimer();
}

void TaskPolarPatternParameters::onOffset(const double offset)
{
    if (blockUpdate) {
        return;
    }
    auto pcPolarPattern = getObject<PartDesign::PolarPattern>();
    pcPolarPattern->Offset.setValue(offset);

    exitSelectionMode();
    kickUpdateViewTimer();
}

void TaskPolarPatternParameters::onOccurrences(const uint n)
{
    if (blockUpdate) {
        return;
    }
    auto pcPolarPattern = getObject<PartDesign::PolarPattern>();
    pcPolarPattern->Occurrences.setValue(n);

    exitSelectionMode();
    kickUpdateViewTimer();
}

void TaskPolarPatternParameters::onAxisChanged(int /*num*/)
{
    if (blockUpdate) {
        return;
    }
    auto pcPolarPattern = getObject<PartDesign::PolarPattern>();

    try {
        if (!axesLinks.getCurrentLink().getValue()) {
            // enter reference selection mode
            hideObject();
            showBase();
            selectionMode = SelectionMode::Reference;
            Gui::Selection().clearSelection();
            addReferenceSelectionGate(AllowSelection::EDGE | AllowSelection::CIRCLE);
        }
        else {
            exitSelectionMode();
            pcPolarPattern->Axis.Paste(axesLinks.getCurrentLink());
        }
    }
    catch (Base::Exception& e) {
        QMessageBox::warning(nullptr, tr("Error"), QApplication::translate("Exception", e.what()));
    }

    kickUpdateViewTimer();
}

void TaskPolarPatternParameters::onUpdateView(bool on)
{
    blockUpdate = !on;
    if (on) {
        // Do the same like in TaskDlgPolarPatternParameters::accept() but without doCommand
        auto pcPolarPattern = getObject<PartDesign::PolarPattern>();
        std::vector<std::string> axes;
        App::DocumentObject* obj = nullptr;

        setupTransaction();
        getAxis(obj, axes);
        pcPolarPattern->Axis.setValue(obj, axes);
        pcPolarPattern->Reversed.setValue(getReverse());
        pcPolarPattern->Angle.setValue(getAngle());
        pcPolarPattern->Occurrences.setValue(getOccurrences());

        recomputeFeature();
    }
}

void TaskPolarPatternParameters::getAxis(App::DocumentObject*& obj,
                                         std::vector<std::string>& sub) const
{
    const App::PropertyLinkSub& lnk = axesLinks.getCurrentLink();
    obj = lnk.getValue();
    sub = lnk.getSubValues();
}

bool TaskPolarPatternParameters::getReverse() const
{
    return ui->checkReverse->isChecked();
}

int TaskPolarPatternParameters::getMode() const
{
    return ui->comboMode->currentIndex();
}

double TaskPolarPatternParameters::getAngle() const
{
    return ui->polarAngle->value().getValue();
}

unsigned TaskPolarPatternParameters::getOccurrences() const
{
    return ui->spinOccurrences->value();
}


TaskPolarPatternParameters::~TaskPolarPatternParameters()
{
    // hide the parts coordinate system axis for selection
    try {
        PartDesign::Body* body = PartDesign::Body::findBodyOf(getObject());
        if (body) {
            App::Origin* origin = body->getOrigin();
            auto vpOrigin = static_cast<ViewProviderCoordinateSystem*>(
                Gui::Application::Instance->getViewProvider(origin));
            vpOrigin->resetTemporaryVisibility();
        }
    }
    catch (const Base::Exception& ex) {
        Base::Console().error("%s\n", ex.what());
    }
}

void TaskPolarPatternParameters::apply()
{
    std::vector<std::string> axes;
    App::DocumentObject* obj = nullptr;
    getAxis(obj, axes);
    std::string axis = buildLinkSingleSubPythonStr(obj, axes);

    auto tobj = getObject();
    FCMD_OBJ_CMD(tobj, "Axis = " << axis.c_str());
    FCMD_OBJ_CMD(tobj, "Reversed = " << getReverse());
    FCMD_OBJ_CMD(tobj, "Mode = " << getMode());
    ui->polarAngle->apply();
    ui->angleOffset->apply();
    ui->spinOccurrences->apply();
}

//**************************************************************************
//**************************************************************************
// TaskDialog
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TaskDlgPolarPatternParameters::TaskDlgPolarPatternParameters(
    ViewProviderPolarPattern* PolarPatternView)
    : TaskDlgTransformedParameters(PolarPatternView)
{
    parameter = new TaskPolarPatternParameters(PolarPatternView);

    Content.push_back(parameter);
}

#include "moc_TaskPolarPatternParameters.cpp"
