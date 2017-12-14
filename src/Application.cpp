//===-- Application.cpp ---------------------------------------------------===//
//
//                                  SPARGEL
//                   Smoothed Particle Generator and Loader
//
// This file is distributed under the GNU General Public License. See LICENSE
// for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Application.cpp
///
//===----------------------------------------------------------------------===//

#include "Application.h"

Application::Application(Arguments *args) : mArgs(args) {}

Application::~Application() {
  mFiles.clear();
  // if (mArgs != NULL) delete mArgs;
  // if (mParams != NULL) delete mParams;
  // if (mOpacity != NULL) delete mOpacity;
  // if (mFNE != NULL) delete mFNE;
  // delete mRA;
}

void Application::StartSplash(void) {
  std::cout << "\n";
  for (int i = 0; i < 16; ++i) std::cout << "=====";
  std::cout << "\n\n";
  std::cout << "   SParGeL\n\n";
  std::cout << "   Smoothed Particle Generator and Loader\n\n";
  for (int i = 0; i < 16; ++i) std::cout << "=====";
  std::cout << "\n\n";
}

void Application::EndSplash(void) {
  for (int i = 0; i < 16; ++i) std::cout << "=====";
  std::cout << "\n";
}

bool Application::Initialise() {
  if (mArgs->GetNumArgs() < 2) {
    std::cout << "A parameter file must be specified, exiting...\n";
    return false;
  }

  mParams = new Parameters();
  mParams->Read(mArgs->GetArgument(0));

  mNumThreads = mParams->GetInt("THREADS");
  mMaxThreads = std::thread::hardware_concurrency();
  if (mMaxThreads <= 0) {
    std::cout << "Number of threads not detected, exiting...\n";
    return false;
  }
  if (mNumThreads < 0 || mNumThreads > mMaxThreads) mNumThreads = mMaxThreads;

  mConvert = mParams->GetInt("CONVERT");
  mInFormat = mParams->GetString("IN_FORMAT");
  mOutFormat = mParams->GetString("OUT_FORMAT");
  mOutput = mParams->GetInt("OUTPUT_FILES");
  mEosFilePath = mParams->GetString("EOS_TABLE");
  mCloudAnalyse = mParams->GetInt("CLOUD_ANALYSIS");
  mCloudCenter = mParams->GetInt("CLOUD_CENTER");
  mDiscAnalyse = mParams->GetInt("DISC_ANALYSIS");
  mSinkAnalyse = mParams->GetInt("SINK_ANALYSIS");
  mCenter = mParams->GetInt("DISC_CENTER");
  mRadialAnalyse = mParams->GetInt("RADIAL_ANALYSIS");

  mOpacity = new OpacityTable(mEosFilePath, true);
  if (!mOpacity->Read()) return false;

  if (mParams->GetInt("GENERATE")) {
    mGenerator = new Generator(mParams, mOpacity);
    mGenerator->Create();
    NameData nd;
    nd.dir = "./";
    nd.id = "SPA";
    nd.format = "column";
    nd.snap = "00000";

    SerenFile *cf = new SerenFile(nd, false);
    cf->SetParticles(mGenerator->GetParticles());
    cf->SetSinks(mGenerator->GetSinks());
    OutputFile(cf, "./disc_column.dat");
    mFiles.push_back(cf);
  }

  if (mCloudAnalyse) {
    mCloudAnalyser = new CloudAnalyser(mParams->GetString("CLOUD_OUTPUT"));
  }

  if (mDiscAnalyse) {
    mDiscAnalyser = new DiscAnalyser();
  }

  if (mSinkAnalyse) {
    mSinkAnalyser = new SinkAnalyser();
  }

  // Create files
  for (int i = 1; i < mArgs->GetNumArgs() - 1; ++i) {
    std::string curArg = mArgs->GetArgument(i);
    mFNE = new FileNameExtractor(curArg);
    NameData nd = mFNE->GetNameData();

    if (mInFormat == "su") {
      mFiles.push_back(new SerenFile(nd, false));
    }
    else if (mInFormat == "sf"){
      mFiles.push_back(new SerenFile(nd, true));
    }
    else if (mInFormat == "du"){
      mFiles.push_back(new DragonFile(nd, false));
    }
    else if (mInFormat == "df"){
      mFiles.push_back(new DragonFile(nd, true));
    }
    else if (mInFormat == "column"){
      mFiles.push_back(new ColumnFile(nd));
    }
    else if (mInFormat == "sink") {
      mFiles.push_back(new SinkFile(nd));
    }
    else {
      std::cout << "Unrecognised input file format, exiting...\n";
      return false;
    }
  }

  if (mFiles.size() < 1) {
    std::cout << "No files selected, exiting...\n";
    return false;
  }
  return true;
}

void Application::Run() {
  // Set up file batches
  if (mFiles.size() < mNumThreads) mNumThreads = mFiles.size();
  mFilesPerThread = mFiles.size() / (mNumThreads);
  mRemainder = mFiles.size() % (mNumThreads);
  std::thread threads[mNumThreads];

  std::cout << "   Threads          : " << mNumThreads << "\n";
  std::cout << "   Files            : " << mFiles.size() << "\n";
  std::cout << "   Files per thread : " << mFilesPerThread << "\n";
  std::cout << "   Remainder        : " << mRemainder << "\n\n";

  std::cout << "   EOS table        : " << mOpacity->GetFileName() << "\n\n";

  int pos = 0;
  for (int i = 0; i < mNumThreads; ++i) {
    int start = pos;
    int end = pos + mFilesPerThread + ((mRemainder > 0) ? 1 : 0);
    pos = end;
    --mRemainder;

    threads[i] = std::thread(&Application::Analyse, this, i, start, end);
  }

  // Join the threads
  for (int i = 0; i < mNumThreads; ++i) {
    threads[i].join();
  }

  if (mCloudAnalyse) mCloudAnalyser->Write();
  if (mSinkAnalyse) {
    mSinkAnalyser->WriteMassRadius();
    mSinkAnalyser->WriteNbody();
  }

  std::cout << "   Files analysed   : " << mFilesAnalysed << "\n\n";
}

void Application::Analyse(int task, int start, int end) {
  for (int i = start; i < end; ++i) {
    // File read
    if (mGenerator == NULL) {
      if (!mFiles.at(i)->Read()) break;
    }

    // Extra quantity calculation
    FindThermo((SnapshotFile *) mFiles.at(i));
    if (mInFormat == "su" || mInFormat == "sf") {
    }
    // Cloud analysis
    if (mCloudAnalyse) {
      mCloudAnalyser->FindCentralQuantities((SnapshotFile *) mFiles.at(i));
      if (mCloudCenter) {
        mCloudAnalyser->CenterAroundDensest((SnapshotFile *) mFiles.at(i));
      }
    }
    // Disc analysis
    if (mDiscAnalyse) {
      if (mCenter) {
        mDiscAnalyser->Center((SnapshotFile *) mFiles.at(i), mCenter - 1);
      }
    }
    // FindColumnDensity((SnapshotFile *) mFiles.at(i));
    FindOpticalDepth((SnapshotFile *) mFiles.at(i));
    // Sink analysis
    if (mSinkAnalyse) {
      mSinkAnalyser->AddMassRadius((SinkFile *) mFiles.at(i));
      mSinkAnalyser->AddNbody((SinkFile *) mFiles.at(i));
    }
    // Radial analysis
    if (mRadialAnalyse) {
      RadialAnalyser *ra = new RadialAnalyser(
        mParams->GetInt("RADIUS_IN"),
        mParams->GetInt("RADIUS_OUT"),
        mParams->GetInt("RADIAL_BINS"),
        mParams->GetInt("RADIAL_LOG"),
        mParams->GetInt("VERTICAL_ANALYSIS"));
      ra->Run((SnapshotFile *) mFiles.at(i));
      delete ra;
    }
    // File conversion
    if (mConvert) {
      mFiles.at(i)->SetNameDataFormat(mOutFormat);
    }
    // Snapshot output
    if (mOutput) {
      OutputFile((SnapshotFile *) mFiles.at(i));
    }
    ++mFilesAnalysed;
    delete mFiles[i];
  }
}

void Application::OutputFile(SnapshotFile *file, std::string fileName) {
  NameData nd = file->GetNameData();
  std::string outputName;

  if (fileName == "") {
    outputName = nd.dir + "/" + nd.id + "." +
    nd.format + "." + nd.snap + nd.append;
  }
  else {
    outputName = fileName;
    nd.format = "column";
  }

  // TODO: reduce code duplication
  if (nd.format == "df") {
    DragonFile *df = new DragonFile(nd, true);
    df->SetParticles(file->GetParticles());
    df->SetSinks(file->GetSinks());
    df->SetNumGas(file->GetNumGas());
    df->SetNumSinks(file->GetNumSinks());
    df->SetNumTot(file->GetNumPart());
    df->SetTime(file->GetTime());
    df->Write(outputName, true);
  }

  if (nd.format == "su") {
    SerenFile *su = new SerenFile(nd, false);
    su->SetParticles(file->GetParticles());
    su->SetSinks(file->GetSinks());
    su->SetNumGas(file->GetNumGas());
    su->SetNumSinks(file->GetNumSinks());
    su->SetNumTot(file->GetNumPart());
    su->SetTime(file->GetTime());
    su->Write(outputName, false);
  }

  if (nd.format == "column") {
    ColumnFile *cf = new ColumnFile(nd);
    cf->SetParticles(file->GetParticles());
    cf->SetSinks(file->GetSinks());
    cf->SetNumGas(file->GetParticles().size());
    cf->SetNumSinks(file->GetSinks().size());
    cf->SetNumTot(file->GetParticles().size() + file->GetSinks().size());
    cf->SetTime(0.0);
    cf->Write(outputName);
  }
}

void Application::FindThermo(SnapshotFile *file) {
  std::vector<Particle *> part = file->GetParticles();
  for (int i = 0; i < part.size(); ++i) {
    Particle *p = part[i];
    FLOAT density = p->GetD();
    FLOAT energy = p->GetU();
    FLOAT sigma = p->GetSigma();
    FLOAT temp = mOpacity->GetTemp(density, energy);
    FLOAT gamma = mOpacity->GetGamma(density, temp);
    FLOAT kappa = mOpacity->GetKappa(density, temp);
    FLOAT kappar = mOpacity->GetKappar(density, temp);
    FLOAT press = (gamma - 1.0) * density * energy;
    FLOAT tau = kappa * sigma;
    FLOAT dudt = (4.0 * SB * (temp - 10.0)) /
                 ((sigma * sigma * kappa) + (1 / kappar));

    part[i]->SetR(part[i]->GetX().Norm());
    part[i]->SetT(temp);
    part[i]->SetP(press);
    part[i]->SetOpacity(kappa);
    part[i]->SetRealOpacity(kappar);
    part[i]->SetTau(tau);
    part[i]->SetCooling(dudt);
  }
  file->SetParticles(part);
}

void Application::FindColumnDensity(SnapshotFile *file) {
  std::vector<Particle *> part = file->GetParticles();
  std::sort(part.begin(), part.end(),
            [](Particle *a, Particle *b) { return b->GetX().z < a->GetX().z; });

  FLOAT h_fac = 4.0;

  // POSITIVE Z
  for (int i = 0; i < part.size(); ++i) {
    Particle *a = part[i];
    if (a->GetX().z < 0.0) break;

    FLOAT search = h_fac * a->GetH();
    FLOAT m = a->GetM();
    FLOAT maxZ = -1E30;
    int Npart = 1;

    for (int j = 0; j < i; ++j) {
      Particle *b = part[j];
      FLOAT dist = (a->GetX() - b->GetX()).Norm2();
      if (dist > search) continue;

      m += b->GetM();

      if (b->GetX().z > maxZ) maxZ = b->GetX().z;

      ++Npart;
    }
    // Sometimes there is no particle above in the neighbour list
    // so the particle is the highest around i.e. it is a surface particle.
    // As such, the distance to the surface is it's own z.
    if (Npart == 1) maxZ = 0.0;

    part.at(i)->SetRealSigma(
      (m / (PI * search * search)) * MSOLPERAU2_TO_GPERCM2);
  }

  // NEGATIVE Z
  for (int i = part.size() - 1; i >= 0; --i) {
    Particle *a = part[i];
    if (a->GetX().z > 0.0) break;

    FLOAT search = h_fac * a->GetH();
    FLOAT m = a->GetM();
    FLOAT minZ = 1E30;
    int Npart = 1;

    for (int j = part.size() - 1; j > i; --j) {
      Particle *b = part[j];
      FLOAT dist = (a->GetX() - b->GetX()).Norm2();
      if (dist > search) continue;

      m += b->GetM();
      if (b->GetX().z < minZ) minZ = b->GetX().z;

      ++Npart;
    }
    if (Npart == 1) minZ = 0.0;

    part[i]->SetRealSigma(
      (m / (PI * search * search)) * MSOLPERAU2_TO_GPERCM2);
  }

  file->SetParticles(part);
}

void Application::FindOpticalDepth(SnapshotFile *file) {
  std::vector<Particle *> part = file->GetParticles();

  OpticalDepthOctree *octree = new OpticalDepthOctree(
    Vec3(0.0, 0.0, 0.0),
    Vec3(1024.0, 1024.0, 1024.0),
    NULL, NULL);

  std::vector<OpticalDepthOctree *> list;

  // Get the maximum distance to a particle
  std::sort(part.begin(), part.end(),
            [](Particle *a, Particle *b) { return b->GetR() < a->GetR(); });
  FLOAT shift = 0.0;//std::ceil(fabs(part[0]->GetR()));

  // Sort by z descending
  std::sort(part.begin(), part.end(),
            [](Particle *a, Particle *b) { return b->GetX().x < a->GetX().x; });

  std::vector<Particle *> positive, negative;
  for (int i = 0; i < part.size(); ++i) {
    if (part[i]->GetX().z >= 0.0) {
      positive.push_back(part[i]);
    }
    else {
      negative.push_back(part[i]);
    }
  }

  // Construct, link and walk twice. First for particles with z > 0. Then take
  // particles with z < 0 and reflect about the z-axis using absolute z values.
  octree->Construct(positive, shift);
  octree->LinkTree(list);
  octree->Walk(positive, shift, mOpacity);
  for (int i = 0; i < positive.size(); ++i) part[i] = positive[i];

  for (int i = 0; i < negative.size(); ++i) {
    Particle *p = negative[i];
    FLOAT x = p->GetX().x;
    FLOAT y = p->GetX().y;
    FLOAT z = -(p->GetX().z);
    negative[i]->SetX(Vec3(x, y, z));
  }
  octree->Construct(negative, shift);
  octree->LinkTree(list);
  octree->Walk(negative, shift, mOpacity);
  // The particles below the disc which have been flipped above now require
  // being flipped back.
  for (int i = 0; i < negative.size(); ++i) {
    FLOAT x = negative[i]->GetX().x;
    FLOAT y = negative[i]->GetX().y;
    FLOAT z = -(negative[i]->GetX().z);
    negative[i]->SetX(Vec3(x, y, z));
    part[i + positive.size()] = negative[i];
  }

  std::sort(part.begin(), part.end(),
  [](Particle *a, Particle *b) { return b->GetID() > a->GetID(); });
  std::ofstream out;
  out.open("new_sigma.dat");
  for (int i = 0; i < part.size(); ++i) {
    Particle *p = part[i];
    if (p->GetRealTau() == 0.0) continue;
    out << p->GetX().Norm() << "\t"     // 0
        << p->GetX().x << "\t"          // 1
        << p->GetX().y << "\t"          // 2
        << p->GetX().z << "\t"          // 3
        << p->GetTau() << "\t"          // 4
        << p->GetRealTau() << "\t"      // 5
        << p->GetD() << "\t"            // 6
        << p->GetT() << "\t"            // 7
        << p->GetQ() << "\t"            // 8
        << p->GetP() << "\t"            // 9
        << p->GetCooling() << "\t"      // 10
        << p->GetRealCooling() << "\t"  // 11
        << p->GetSigma() << "\t"        // 12
        << p->GetRealSigma() << "\n";   // 13
  }
  out.close();

  file->SetParticles(part);

  delete octree;
}
