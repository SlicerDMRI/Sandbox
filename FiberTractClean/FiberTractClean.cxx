#if defined(_MSC_VER)
#pragma warning ( disable : 4786 )
#endif

#ifdef __BORLANDC__
#define ITK_LEAN_AND_MEAN
#endif

#include "FiberTractCleanCLP.h"

#include "itkPluginFilterWatcher.h"
#include "itkPluginUtilities.h"

// vtkITK includes
#include <vtkITKArchetypeImageSeriesScalarReader.h>

// VTK includes
#include <vtkCellArray.h>
#include <vtkFloatArray.h>
#include <vtkInformation.h>
#include <vtkImageCast.h>
#include <vtkImageData.h>
#include <vtkMath.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkPolyDataWriter.h>
#include <vtkSmartPointer.h>
#include <vtkStreamingDemandDrivenPipeline.h>
#include <vtkTimerLog.h>
#include <vtkTransform.h>
#include <vtkTransformPolyDataFilter.h>
#include <vtkXMLPolyDataReader.h>
#include <vtkXMLPolyDataWriter.h>
#include <vtkPolyDataReader.h>
#include <vtkImageDilateErode3D.h>
#include <vtkImageThreshold.h>
#include <vtkVersion.h>

#include <vtkNIFTIImageWriter.h>

// STD includes
#include <iostream>
#include <algorithm>
#include <string>

// VTKSYS includes
#include <vtksys/SystemTools.hxx>


int main( int argc, char * argv[] )
{
  PARSE_ARGS;
 
  try
  {
  std::cerr << "Start cleaning 01 / 12 : reading parameter setting..." << std::endl; 
  // PARAMETERS: test they have been typed in okay
  // size (in voxels) of kernel for mask erosion
  int kernelSize = KernelSize;
  // .9 means must be 90% inside the eroded brain mask to keep it
  float maskPercent = PercentInsideMask;
  // e.g. 2 means to throw out fibers of length < 2 (single points)
  int pointsThreshold = MinimumNumberOfPoints;
  bool verbose = Verbose;

  std::cerr << "   -- KernelSize = " << KernelSize
            << ", maskPercent = " << maskPercent 
            << ", pointsThreshold = " << pointsThreshold
            << std::endl; 

  if (maskPercent < 0.0 || maskPercent > 1.0)
    {
    std::cerr << "Mask percent is not between 0 and 1: " << maskPercent << std::endl;
    return EXIT_FAILURE;
    }

  // int excludeOperation = 0; // 0-AND; 1-OR
  // if (NoPassOperation == std::string("OR"))
  //   {
  //   excludeOperation = 0;
  //   }
  // else if (NoPassOperation == std::string("AND"))
  //   {
  //   excludeOperation = 1;
  //   }
  // else
  //   {
  //   std::cerr << "unknown exclude operation";
  //   return EXIT_FAILURE;
  //   }

  std::cerr << "Start cleaning 02 / 12 : loading mask information..." << std::endl; 

  // Read in Label volume inputs
  vtkNew<vtkImageCast> imageCastLabel_A;
  vtkNew<vtkITKArchetypeImageSeriesScalarReader> readerLabel_A;
  vtkNew<vtkImageDilateErode3D> erodeLabel_A;
  vtkNew<vtkImageThreshold> thresholdLabel_A;

  readerLabel_A->SetArchetype(InputLabel_A.c_str());
  readerLabel_A->SetUseOrientationFromFile(1);
  readerLabel_A->SetUseNativeOriginOn();
  readerLabel_A->SetOutputScalarTypeToNative();
  readerLabel_A->SetDesiredCoordinateOrientationToNative();
  readerLabel_A->SetSingleFile(1);
  readerLabel_A->Update();

  // Cast (so the below code can assume short)
  imageCastLabel_A->SetOutputScalarTypeToShort();
  #if (VTK_MAJOR_VERSION <= 5)
    imageCastLabel_A->SetInput(readerLabel_A->GetOutput() );
  #else
    imageCastLabel_A->SetInputConnection(readerLabel_A->GetOutputPort() );
  #endif
    imageCastLabel_A->Update();


  // vtkNew<vtkNIFTIImageWriter> writertest;
  // writertest->SetInputData( imageCastLabel_A->GetOutput() );
  // writertest->SetFileName( "TEST_CAST.nii" );
  // writertest->Write();

  // threshold so all nonzero values are considered in the mask
  // so below code for erosion can assume 1 is the label of interest
  #if (VTK_MAJOR_VERSION <= 5)
    thresholdLabel_A->SetInput(imageCastLabel_A->GetOutput() );
  #else
    thresholdLabel_A->SetInputConnection(imageCastLabel_A->GetOutputPort() );
  #endif
  //The values greater than or equal to the value match.
  thresholdLabel_A->ThresholdByUpper(1);
  thresholdLabel_A->ReplaceInOn();
  thresholdLabel_A->SetInValue(1);
  thresholdLabel_A->Update();

  // writertest->SetInputData( thresholdLabel_A->GetOutput() );
  // writertest->SetFileName( "TEST_THRESHOLD.nii" );
  // writertest->Write();

  #if (VTK_MAJOR_VERSION <= 5)
    erodeLabel_A->SetInput(thresholdLabel_A->GetOutput() );
  #else
    erodeLabel_A->SetInputConnection(thresholdLabel_A->GetOutputPort() );
  #endif
  erodeLabel_A->SetDilateValue(0);
  erodeLabel_A->SetErodeValue(1);
  // Kernel size parameter from user
  erodeLabel_A->SetKernelSize(kernelSize, kernelSize, kernelSize);
  erodeLabel_A->Update();

  std::cerr << "Start cleaning 03 / 12 : loading fiber information..." << std::endl; 

  // writertest->SetInputData( erodeLabel_A->GetOutput() );
  // writertest->SetFileName( "TEST_ERODE.nii" );
  // writertest->Write();

  // Read in fiber bundle input to be selected.
  // CLIs from within Slicer do all I/O with vtp format, but often the command-line preference is vtk for compatibility with other tools
  std::string extension1 = vtksys::SystemTools::GetFilenameLastExtension(InputFibers.c_str());
  std::string extension = vtksys::SystemTools::LowerCase(extension1);
  // The above two lines duplicate the below function, but we can't include vtkMRMLStorageNode.h here.
  //std::string extension = vtkMRMLStorageNode::GetLowercaseExtensionFromFileName(InputFibers.c_str());
  // This smart pointer should prevent the reader from being deleted
  vtkSmartPointer<vtkPolyData> input;
  if (extension == std::string(".vtk"))
    {
    vtkPolyData* output = 0;
    vtkNew<vtkPolyDataReader> readerPD;
    readerPD->SetFileName(InputFibers.c_str());
    readerPD->Update();
    input = vtkPolyData::SafeDownCast(readerPD->GetOutput());
    }
  else if (extension == std::string(".vtp"))
    {
      vtkNew<vtkXMLPolyDataReader> readerPD;
      readerPD->SetFileName(InputFibers.c_str());
      readerPD->Update();
      input = vtkPolyData::SafeDownCast(readerPD->GetOutput());
      }

  //vtkNew<vtkXMLPolyDataReader> readerPD;
  //readerPD->SetFileName(InputFibers.c_str());
  //readerPD->Update();

  std::cerr << "Start cleaning 04 / 12 : ijk space transfroming..." << std::endl; 

  //1. Set up matrices to put fibers into ijk space of volume
  // This assumes fibers are in RAS space of volume (i.e. RAS==world)
  vtkNew<vtkMatrix4x4> Label_A_RASToIJK;
  Label_A_RASToIJK->DeepCopy(readerLabel_A->GetRasToIjkMatrix());

  //the volume we're probing knows its spacing so take this out of the matrix
  /**
  //double sp[3];
  //erodeLabel_A->GetOutput()->GetSpacing(sp);
  **/
  vtkNew<vtkTransform> trans;
  trans->Identity();
  trans->PreMultiply();
  trans->SetMatrix(Label_A_RASToIJK.GetPointer());

  /**
  // Trans from IJK to RAS
  trans->Inverse();
  // Take into account spacing to compute Scaled IJK
  trans->Scale(1/sp[0],1/sp[1],1/sp[2]);
  // now it's RAS to scaled IJK
  trans->Inverse();
 ***/

  std::cerr << "Start cleaning 05 / 12 : finding polylines..." << std::endl; 

  // 2. Find polylines
  int inExt[6];
  #if (VTK_MAJOR_VERSION <= 5)
    erodeLabel_A->GetOutput()->GetWholeExtent(inExt);
  #else
    erodeLabel_A->GetOutputInformation(0)->Get(
      vtkStreamingDemandDrivenPipeline::WHOLE_EXTENT(), inExt);
  #endif

  vtkPoints *inPts =input->GetPoints();
  vtkIdType numPts = inPts->GetNumberOfPoints();
  vtkCellArray *inLines = input->GetLines();
  vtkIdType numLines = inLines->GetNumberOfCells();
  vtkIdType npts=0, *pts=NULL;

  if ( !inPts || numPts  < 1 || !inLines || numLines < 1 )
    {
    return EXIT_SUCCESS;
    }

  std::vector<int> addLines;
  vtkIdType numNewPts[3];
  numNewPts[0] = 0;
  numNewPts[1] = 0;
  numNewPts[2] = 0;
  vtkIdType numNewCells[3];
  numNewCells[0] = 0;
  numNewCells[1] = 0;
  numNewCells[2] = 0;
  vtkIdType j;
  double p[3];

  int *labelDims = erodeLabel_A->GetOutput()->GetDimensions();

  //int countShortFibers = 0;
  //int countOutsideMask = 0;
    
  std::cerr << "Start cleaning 06 / 12 : checking lines..." << std::endl; 

  // Check lines
  vtkIdType inCellId;
  for (inCellId=0, inLines->InitTraversal();
       inLines->GetNextCell(npts,pts); inCellId++)
    {
    if (npts <= pointsThreshold)
      {
      addLines.push_back(0);
      if (verbose)
        {
        std::cerr << "Less than " << pointsThreshold << 
                " points in line " << inCellId << std::endl;
        }

      numNewPts[0] += npts;
      numNewCells[0]++;

      continue; //skip this polyline
      }
    double pIJK[3];
    int pt[3];
    short *inPtr;
    int addLine = -1;
    //bool pass = false;
    //bool nopass = false;
    int inMaskSum = 0;
    for (j=0; j < npts; j++)
      {
      inPts->GetPoint(pts[j],p);
      trans->TransformPoint(p,pIJK);
      pt[0]= (int) floor(pIJK[0]);
      pt[1]= (int) floor(pIJK[1]);
      pt[2]= (int) floor(pIJK[2]);
      if (pt[0] < 0 || pt[1] < 0 || pt[2] < 0 ||
          pt[0] >= labelDims[0] || pt[1] >= labelDims[1] || pt[2] >= labelDims[2])
        {
        if (verbose)
          {
            std::cerr << "point #" << j <<" on the line #" << inCellId << " is outside the mask volume" << std::endl;
            std::cerr << "IJK point: " << pIJK[0] << " " <<  pIJK[1] << " " <<  pIJK[2]  << std::endl;
            std::cerr << "point: " << pt[0] << " " <<  pt[1] << " " <<  pt[2]  << std::endl;
            std::cerr << "dims: " << labelDims[0] << " " <<  labelDims[1] << " " <<  labelDims[2] << std::endl;
          }
        continue;
        }
      inPtr = (short *) erodeLabel_A->GetOutput()->GetScalarPointer(pt);
      if (*inPtr == short(1))
        {
          inMaskSum+=1;
        }
      } //for (j=0; j < npts; j++)

    if ((float(inMaskSum)/float(npts)) > maskPercent)
      {
      addLine = 2;
      }
    else
      {
      addLine = 1;
      }

  
    if (verbose)
      {
      std::cerr << "line #" << inCellId << " is " << (float(inMaskSum)/float(npts)) << " percent inside the mask " << addLine << " " << inMaskSum << " " << npts << " " << *inPtr << std::endl;
      }

    addLines.push_back(addLine);
    numNewPts[addLine] += npts;
    numNewCells[addLine] ++;
    } //for (inCellId=0, inLines->InitTraversal();

  std::cerr << "Start cleaning 07 / 12 : processed reresults" << std::endl; 
  // Add lines

  std::cerr << "  -- " <<
      " Fibers kept: " << numNewCells[2] << "/" << input->GetNumberOfLines() << 
      ". Short fibers: " << numNewCells[0] << 
      ". Outside eroded mask: " << numNewCells[1] << std::endl;

  // std::cerr << "Start cleaning 7.1" << std::endl; 
  //Preallocate PolyData elements
  // vtkNew<vtkPolyData> outFibers0;
  // vtkNew<vtkPoints> points0;
  // vtkNew<vtkCellArray> outFibersCellArray0;
  // points0->Allocate(numNewPts[0]);
  // outFibers0->SetPoints(points0.GetPointer());
  // outFibersCellArray0->Allocate(numNewPts[0]+numNewCells[0]);
  // outFibers0->SetLines(outFibersCellArray0.GetPointer());

  std::cerr << "Start cleaning 08 / 12 : preparing outputs..." << std::endl; 
  
  vtkNew<vtkPolyData> outFibers1;
  vtkNew<vtkPoints> points1;
  vtkNew<vtkCellArray> outFibersCellArray1;
  points1->Allocate(numNewPts[1]);
  outFibers1->SetPoints(points1.GetPointer());
  outFibersCellArray1->Allocate(numNewPts[1]+numNewCells[1]);
  outFibers1->SetLines(outFibersCellArray1.GetPointer());

  vtkNew<vtkPolyData> outFibers2;
  vtkNew<vtkPoints> points2;
  vtkNew<vtkCellArray> outFibersCellArray2;
  points2->Allocate(numNewPts[2]);
  outFibers2->SetPoints(points2.GetPointer());
  outFibersCellArray2->Allocate(numNewPts[2]+numNewCells[2]);
  outFibers2->SetLines(outFibersCellArray2.GetPointer());

  std::cerr << "Start cleaning 09 / 12 : copying tensors or scalar arrays..." << std::endl; 
  // If the input has point data, including tensors or scalar arrays, copy them to the output.
  // Currently this ignores cell data, which may be added in the future if needed.
  // Check for point data arrays to keep and allocate them.
  int numberArrays = input->GetPointData()->GetNumberOfArrays();
  // std::cerr << vtksys::SystemTools::GetFilenameName(OutputKeptFibers.c_str()) << 
  //  ":: Fibers kept: " << numNewCells[2] << "/" << input->GetNumberOfLines() << 
  //    ". Short fibers: " << numNewCells[0] << 
  //    ". Outside eroded mask: " << numNewCells[1] << 
  //    ". Data arrays: " << numberArrays << std::endl;

  for (int arrayIdx = 0; arrayIdx < numberArrays; arrayIdx++)
    {
      //std::cerr << "Found array " << arrayIdx << std::endl;
      vtkDataArray *oldArray = input->GetPointData()->GetArray(arrayIdx);
      vtkSmartPointer<vtkFloatArray> newArray = vtkSmartPointer<vtkFloatArray>::New();
      newArray->SetNumberOfComponents(oldArray->GetNumberOfComponents());
      newArray->SetName(oldArray->GetName());
      
      // newArray->Allocate(newArray->GetNumberOfComponents()*numNewPts[0]);
      // outFibers0->GetPointData()->AddArray(newArray);

      newArray->Allocate(newArray->GetNumberOfComponents()*numNewPts[1]);
      outFibers1->GetPointData()->AddArray(newArray);

      newArray->Allocate(newArray->GetNumberOfComponents()*numNewPts[2]);
      outFibers2->GetPointData()->AddArray(newArray);

      if (verbose)
        {
        std::cerr << "Output array " << newArray->GetName() << 
                   " created with " << newArray->GetNumberOfComponents() << 
                   " components." << std::endl;
        }
    }


  std::cerr << "Start cleaning 10 / 12 : copying fibers..." << std::endl; 
  vtkIdType ptId[3];
  ptId[0] = 0;
  ptId[1] = 0;
  ptId[2] = 0;
  double tensor[9];

  for (inCellId=0, inLines->InitTraversal();
       inLines->GetNextCell(npts,pts); inCellId++)
    {
     if (addLines[inCellId]==1)
     {
        outFibersCellArray1->InsertNextCell(npts);
        for (j=0; j < npts; j++)
          {
          inPts->GetPoint(pts[j],p);
          points1->InsertPoint(ptId[addLines[inCellId]],p);
          outFibersCellArray1->InsertCellPoint(ptId[addLines[inCellId]]);
    
          // Copy point data from input fiber
          for (int arrayIdx = 0; arrayIdx < numberArrays; arrayIdx++)
            {
              vtkDataArray *newArray = outFibers1->GetPointData()->GetArray(arrayIdx);
              vtkDataArray *oldArray = input->GetPointData()->GetArray(newArray->GetName());
              newArray->InsertNextTuple(oldArray->GetTuple(pts[j]));
            }

          ptId[addLines[inCellId]]++;
          }
     }
     else if (addLines[inCellId]==2)
     {
        outFibersCellArray2->InsertNextCell(npts);
        for (j=0; j < npts; j++)
          {
          inPts->GetPoint(pts[j],p);
          points2->InsertPoint(ptId[addLines[inCellId]],p);
          outFibersCellArray2->InsertCellPoint(ptId[addLines[inCellId]]);
    
          // Copy point data from input fiber
          for (int arrayIdx = 0; arrayIdx < numberArrays; arrayIdx++)
            {
              vtkDataArray *newArray = outFibers2->GetPointData()->GetArray(arrayIdx);
              vtkDataArray *oldArray = input->GetPointData()->GetArray(newArray->GetName());
              newArray->InsertNextTuple(oldArray->GetTuple(pts[j]));
            }

          ptId[addLines[inCellId]]++;
          }
     }

    //  {
      
     // }
    }
  
  std::cerr << "Start cleaning 11 / 12 : copying points..." << std::endl; 
    // Copy array attributes from input (TENSORS, scalars, etc)
  for (int arrayIdx = 0; arrayIdx < numberArrays; arrayIdx++)
    {
      int attr = input->GetPointData()->IsArrayAnAttribute(arrayIdx);
      //std::cerr << input->GetPointData()->GetArray(arrayIdx)->GetName() << "Attribute: " << attr << std::endl;
      if (attr >= 0)
      {
        // outFibers0->GetPointData()->SetActiveAttribute(input->GetPointData()->GetArray(arrayIdx)->GetName(), attr);
        outFibers1->GetPointData()->SetActiveAttribute(input->GetPointData()->GetArray(arrayIdx)->GetName(), attr);
        outFibers2->GetPointData()->SetActiveAttribute(input->GetPointData()->GetArray(arrayIdx)->GetName(), attr);
      }
    }

  std::cerr << "Start cleaning 12 / 12 : storing outputs..." << std::endl; 
  //3. Save the output
  // CLIs from within Slicer do all I/O with vtp format, but often the command-line preference is vtk for compatibility with other tools
  std::string extension2 = vtksys::SystemTools::GetFilenameLastExtension(OutputKeptFibers.c_str());
  std::string extension_output = vtksys::SystemTools::LowerCase(extension2);
  // The above two lines duplicate the below function, but we can't include vtkMRMLStorageNode.h here.
  //std::string extension = vtkMRMLStorageNode::GetLowercaseExtensionFromFileName(InputFibers.c_str());
  if (extension_output == std::string(".vtk"))
    {
    vtkNew<vtkPolyDataWriter> writer;
    writer->SetFileName(OutputKeptFibers.c_str());

    #if (VTK_MAJOR_VERSION <= 5)
         writer->SetInput(outFibers2.GetPointer());
    #else
         writer->SetInputData(outFibers2.GetPointer());
    #endif

    writer->SetFileTypeToBinary();
    writer->Write();
    }
  else if (extension_output == std::string(".vtp"))
    {
    vtkNew<vtkXMLPolyDataWriter> writer;
    writer->SetFileName(OutputKeptFibers.c_str());
    #if (VTK_MAJOR_VERSION <= 5)
         writer->SetInput(outFibers2.GetPointer());
    #else
         writer->SetInputData(outFibers2.GetPointer());
    #endif
    writer->SetDataModeToBinary();
    writer->Write();
    }

  std::string extension21 = vtksys::SystemTools::GetFilenameLastExtension(OutputRemovedFibers.c_str());
  std::string extension_output1 = vtksys::SystemTools::LowerCase(extension21);
  // The above two lines duplicate the below function, but we can't include vtkMRMLStorageNode.h here.
  //std::string extension = vtkMRMLStorageNode::GetLowercaseExtensionFromFileName(InputFibers.c_str());
  if (extension_output1 == std::string(".vtk"))
    {
    vtkNew<vtkPolyDataWriter> writer;
    writer->SetFileName(OutputRemovedFibers.c_str());
    #if (VTK_MAJOR_VERSION <= 5)
         writer->SetInput(outFibers1.GetPointer());
    #else
         writer->SetInputData(outFibers1.GetPointer());
    #endif
    writer->SetFileTypeToBinary();
    writer->Write();
    }
  else if (extension_output1 == std::string(".vtp"))
    {
    vtkNew<vtkXMLPolyDataWriter> writer;
    writer->SetFileName(OutputRemovedFibers.c_str());
    #if (VTK_MAJOR_VERSION <= 5)
         writer->SetInput(outFibers1.GetPointer());
    #else
         writer->SetInputData(outFibers1.GetPointer());
    #endif
    writer->SetDataModeToBinary();
    writer->Write();
    }

    std::cerr << "Completed!!!" << std::endl; 

  }
  catch ( ... )
      {
        std::cerr << "default exception";
        return EXIT_FAILURE;
      }

  return EXIT_SUCCESS;

}
