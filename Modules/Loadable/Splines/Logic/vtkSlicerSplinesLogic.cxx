/*==============================================================================

Program: 3D Slicer

Copyright (c) Kitware Inc.

See COPYRIGHT.txt
or http://www.slicer.org/copyright/copyright.txt for details.

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

This file was originally developed by Julien Finet, Kitware Inc.
and was partially funded by Allen Institute

==============================================================================*/

// Splines Logic includes
#include "vtkSlicerSplinesLogic.h"

// MRML includes
#include <vtkMRMLModelNode.h>
#include <vtkMRMLScene.h>
#include <vtkMRMLSelectionNode.h>

// VTK includes
#include <vtkEventBroker.h>
#include <vtkIntArray.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPolyData.h>
#include <vtkTransform.h>
#include <vtkTransformPolyDataFilter.h>
#include <vtkTriangle.h>
#include <vtkPoints.h>
#include <vtkCellArray.h>
#include <vtkPolyDataWriter.h>
#include <vtkAppendPolyData.h>
#include <vtkCleanPolyData.h>
#include <vtkContourTriangulator.h>

// Splines includes
#include "vtkMRMLMarkupsSplinesNode.h"
#include "vtkMRMLMarkupsSplinesStorageNode.h"

// STD includes
#include <cassert>

//----------------------------------------------------------------------------
vtkStandardNewMacro(vtkSlicerSplinesLogic);

//----------------------------------------------------------------------------
vtkSlicerSplinesLogic::vtkSlicerSplinesLogic()
{
}

//----------------------------------------------------------------------------
vtkSlicerSplinesLogic::~vtkSlicerSplinesLogic()
{
}

//----------------------------------------------------------------------------
void vtkSlicerSplinesLogic::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//---------------------------------------------------------------------------
vtkMRMLSelectionNode* vtkSlicerSplinesLogic::GetSelectionNode() const
{
  if (!this->GetMRMLScene())
  {
    return NULL;
  }

  // try the application logic first
  vtkMRMLApplicationLogic *mrmlAppLogic = this->GetMRMLApplicationLogic();
  return mrmlAppLogic ?
    (mrmlAppLogic->GetSelectionNode() ?
      mrmlAppLogic->GetSelectionNode() : NULL) : NULL;
}

//---------------------------------------------------------------------------
bool vtkSlicerSplinesLogic
::GetCentroid(vtkMRMLMarkupsSplinesNode* splinesNode, int n, double centroid[3])
{
  if (!splinesNode || !splinesNode->MarkupExists(n))
  {
    return false;
  }
  int numberOfPoints = splinesNode->GetNumberOfPointsInNthMarkup(n);
  if (numberOfPoints <= 0)
  {
    return false;
  }

  centroid[0] = 0.0;
  centroid[1] = 0.0;
  centroid[2] = 0.0;
  for (int i = 0; i < numberOfPoints; ++i)
  {
    double point[4];
    splinesNode->GetMarkupPointWorld(n, i, point);
    vtkMath::Add(point, centroid, centroid);
  }
  vtkMath::MultiplyScalar(centroid, 1.0/numberOfPoints);
  return true;
}

//---------------------------------------------------------------------------
char* vtkSlicerSplinesLogic
::LoadMarkupsSplines(const char *fileName, const char *name)
{
  char *nodeID = NULL;
  std::string idList;
  if (!fileName)
  {
    vtkErrorMacro("LoadMarkupSplines: null file name, cannot load");
    return nodeID;
  }

  // turn on batch processing
  this->GetMRMLScene()->StartState(vtkMRMLScene::BatchProcessState);

  // make a storage node and fiducial node and set the file name
  vtkNew<vtkMRMLMarkupsSplinesStorageNode> storageNode;
  storageNode->SetFileName(fileName);
  vtkNew<vtkMRMLMarkupsSplinesNode> splinesNode;
  splinesNode->SetName(name);

  // add the nodes to the scene and set up the observation on the storage node
  this->GetMRMLScene()->AddNode(storageNode.GetPointer());
  this->GetMRMLScene()->AddNode(splinesNode.GetPointer());
  splinesNode->SetAndObserveStorageNodeID(storageNode->GetID());

  // read the file
  if (storageNode->ReadData(splinesNode.GetPointer()))
    {
    nodeID = splinesNode->GetID();
    }

  // turn off batch processing
  this->GetMRMLScene()->EndState(vtkMRMLScene::BatchProcessState);
  return nodeID;
}

//---------------------------------------------------------------------------
vtkSmartPointer<vtkPolyData> vtkSlicerSplinesLogic::CreateModelFromContour(
  vtkPolyData* inputContour, vtkVector3d normal, double thickness)
{
  if (!inputContour
    || !inputContour->GetPoints()
    || inputContour->GetPoints()->GetNumberOfPoints() < 3)
  {
    return nullptr;
  }

  vtkNew<vtkContourTriangulator> contourTriangulator;
  contourTriangulator->SetInputData(inputContour);

  vtkNew<vtkTransform> topHalfTransform;
  topHalfTransform->Translate(
    thickness * 0.5 * normal.GetX(),
    thickness * 0.5 * normal.GetY(),
    thickness * 0.5 * normal.GetZ());
  vtkNew<vtkTransform> bottomHalfTransform;
  bottomHalfTransform->Translate(
    -thickness * 0.5 * normal.GetX(),
    -thickness * 0.5 * normal.GetY(),
    -thickness * 0.5 * normal.GetZ());

  vtkNew<vtkTransformPolyDataFilter> topHalfFilter;
  topHalfFilter->SetInputConnection(contourTriangulator->GetOutputPort());
  topHalfFilter->SetTransform(topHalfTransform.GetPointer());
  topHalfFilter->Update();

  vtkNew<vtkTransformPolyDataFilter> bottomHalfFilter;
  bottomHalfFilter->SetInputConnection(contourTriangulator->GetOutputPort());
  bottomHalfFilter->SetTransform(bottomHalfTransform.GetPointer());
  bottomHalfFilter->Update();

  vtkPolyData* topHalf = topHalfFilter->GetOutput();
  vtkPolyData* bottomHalf = bottomHalfFilter->GetOutput();
  vtkNew<vtkPoints> points;
  for (vtkIdType i = 0; i < topHalf->GetNumberOfPoints(); ++i)
  {
    points->InsertNextPoint(topHalf->GetPoint(i));
    points->InsertNextPoint(bottomHalf->GetPoint(i));
  }

  vtkNew<vtkCellArray> cells;
  for (vtkIdType i = 0; i < points->GetNumberOfPoints() - 2; i += 2)
  {
    vtkNew<vtkTriangle> beltTriangle1;
    beltTriangle1->GetPointIds()->SetId(0, i);
    beltTriangle1->GetPointIds()->SetId(1, i + 1);
    beltTriangle1->GetPointIds()->SetId(2, i + 2);
    cells->InsertNextCell(beltTriangle1.GetPointer());

    vtkNew<vtkTriangle> beltTriangle2;
    beltTriangle2->GetPointIds()->SetId(0, i + 1);
    beltTriangle2->GetPointIds()->SetId(1, i + 3);
    beltTriangle2->GetPointIds()->SetId(2, i + 2);
    cells->InsertNextCell(beltTriangle2.GetPointer());
  }

  vtkNew<vtkPolyData> beltSurface;
  beltSurface->SetPoints(points.GetPointer());
  beltSurface->SetPolys(cells.GetPointer());

  vtkNew<vtkAppendPolyData> appendFilter;
  appendFilter->AddInputConnection(bottomHalfFilter->GetOutputPort());
  appendFilter->AddInputData(beltSurface);
  appendFilter->AddInputConnection(topHalfFilter->GetOutputPort());

  vtkNew<vtkCleanPolyData> cleanFilter;
  cleanFilter->SetInputConnection(appendFilter->GetOutputPort());
  cleanFilter->Update();

  return cleanFilter->GetOutput();
}

//---------------------------------------------------------------------------
void vtkSlicerSplinesLogic::SetMRMLSceneInternal(vtkMRMLScene* newScene)
{
  vtkNew<vtkIntArray> sceneEvents;
  sceneEvents->InsertNextValue(vtkMRMLScene::NodeAddedEvent);
  sceneEvents->InsertNextValue(vtkMRMLScene::NodeRemovedEvent);

  this->SetAndObserveMRMLSceneEventsInternal(newScene, sceneEvents.GetPointer());
}

//---------------------------------------------------------------------------
void vtkSlicerSplinesLogic::ObserveMRMLScene()
{
  if (!this->GetMRMLScene())
  {
    return;
  }

  // add known markup types to the selection node
  vtkMRMLSelectionNode *selectionNode =
    vtkMRMLSelectionNode::SafeDownCast(this->GetSelectionNode());
  if (selectionNode)
  {
    // got into batch process mode so that an update on the mouse mode tool
    // bar is triggered when leave it
    this->GetMRMLScene()->StartState(vtkMRMLScene::BatchProcessState);

    selectionNode->AddNewPlaceNodeClassNameToList(
      "vtkMRMLMarkupsSplinesNode", ":/Icons/SplinesMouseModePlace.png", "Splines");

    // trigger an update on the mouse mode toolbar
    this->GetMRMLScene()->EndState(vtkMRMLScene::BatchProcessState);
  }

  this->Superclass::ObserveMRMLScene();
}

//-----------------------------------------------------------------------------
void vtkSlicerSplinesLogic::RegisterNodes()
{
  vtkMRMLScene* scene = this->GetMRMLScene();
  assert(scene != 0);
  scene->RegisterNodeClass(vtkSmartPointer<vtkMRMLMarkupsSplinesNode>::New());
  scene->RegisterNodeClass(vtkSmartPointer<vtkMRMLMarkupsSplinesStorageNode>::New());
}

//----------------------------------------------------------------------------
void vtkSlicerSplinesLogic::OnMRMLSceneNodeAdded(vtkMRMLNode* node)
{
  vtkMRMLMarkupsSplinesNode* splinesNode =
    vtkMRMLMarkupsSplinesNode::SafeDownCast(node);
  if (!splinesNode)
  {
    return;
  }

  vtkEventBroker::GetInstance()->AddObservation(
    splinesNode, vtkCommand::ModifiedEvent, this, this->GetMRMLNodesCallbackCommand());
  this->UpdateSlabModelNode(splinesNode);
}

//----------------------------------------------------------------------------
void vtkSlicerSplinesLogic::OnMRMLSceneNodeRemoved(vtkMRMLNode* node)
{
  vtkMRMLMarkupsSplinesNode* splinesNode =
    vtkMRMLMarkupsSplinesNode::SafeDownCast(node);
  if (!splinesNode)
  {
    return;
  }

  vtkEventBroker::GetInstance()->RemoveObservations(
    splinesNode, vtkCommand::ModifiedEvent, this, this->GetMRMLNodesCallbackCommand());

  // Remove all the associated model nodes
  for (int i = 0; i < splinesNode->GetNumberOfMarkups(); ++i)
  {
    std::string modelID = splinesNode->GetNthMarkupAssociatedNodeID(i);
    vtkMRMLModelNode* modelNode =
      vtkMRMLModelNode::SafeDownCast(this->GetMRMLScene()->GetNodeByID(modelID));
    if (modelNode)
    {
      this->GetMRMLScene()->RemoveNode(modelNode);
    }
  }
}

//-----------------------------------------------------------------------------
void vtkSlicerSplinesLogic::OnMRMLNodeModified(vtkMRMLNode* node)
{
  vtkMRMLMarkupsSplinesNode* splinesNode =
    vtkMRMLMarkupsSplinesNode::SafeDownCast(node);
  if (!splinesNode)
  {
    return;
  }

  this->UpdateSlabModelNode(splinesNode);
}

//-----------------------------------------------------------------------------
void vtkSlicerSplinesLogic::UpdateSlabModelNode(vtkMRMLMarkupsSplinesNode* splinesNode)
{
  if (!splinesNode)
  {
    return;
  }

  for (int i = 0; i < splinesNode->GetNumberOfMarkups(); ++i)
  {
    std::string modelID = splinesNode->GetNthMarkupAssociatedNodeID(i);
    vtkMRMLModelNode* modelNode =
      vtkMRMLModelNode::SafeDownCast(this->GetMRMLScene()->GetNodeByID(modelID));

    bool shouldHaveModel = splinesNode->GetNumberOfPointsInNthMarkup(i) > 2;

    // Cases:
    // 1- Should have model && model exits -> All good             | DO NOTHING
    // 2- Should not have model && model doesn't exist -> All good | DO NOTHING
    // 3- Should have model && model doesn't exist -> Add it
    // 4- Should not have model && model exist -> Remove it

    if (shouldHaveModel && !modelNode)
    {
      std::stringstream modelName;
      modelName << splinesNode->GetName() << "_Model_" << i;

      vtkMRMLModelNode* newModelNode = vtkMRMLModelNode::SafeDownCast(
        this->GetMRMLScene()->AddNewNodeByClass(
          "vtkMRMLModelNode", modelName.str()));
      splinesNode->SetNthMarkupAssociatedNodeID(i, newModelNode->GetID());
    }
    else if (!shouldHaveModel && modelNode)
    {
      this->GetMRMLScene()->RemoveNode(modelNode);
      splinesNode->SetNthMarkupAssociatedNodeID(i, "");
    }
  }
}
