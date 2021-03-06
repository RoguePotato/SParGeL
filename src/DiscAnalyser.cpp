//===-- DiscAnalyser.cpp --------------------------------------------------===//
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
/// DiscAnalyser.cpp
///
//===----------------------------------------------------------------------===//

#include "DiscAnalyser.h"

DiscAnalyser::DiscAnalyser(Parameters *params) : mParams(params) {}

DiscAnalyser::~DiscAnalyser() {}

void DiscAnalyser::Center(SnapshotFile *file, int sinkIndex, Vec3 posCenter,
                          int densest) {
  std::vector<Particle *> part = file->GetParticles();
  std::vector<Sink *> sink = file->GetSinks();
  Vec3 dX = {0.0, 0.0, 0.0};
  Vec3 dV = {0.0, 0.0, 0.0};
  std::string appendage = ".centered.";

  // Return if there is no position to center around
  if (posCenter.Norm() == 0.0 && (sinkIndex < 0 || sinkIndex >= sink.size()) &&
      !densest) {
    return;
  }

  if (densest) {
    // Sort by density, but use a copy of the particles.
    std::vector<Particle *> dens_sorted = part;
    std::sort(dens_sorted.begin(), dens_sorted.end(),
              [](Particle *a, Particle *b) { return b->GetD() < a->GetD(); });

    // Find the densest however so-many particles CoM.
    float total_mass = 0.0f;
    int n_part = std::max(1, mParams->GetInt("CENTER_DENSEST_NUM"));
    for (int i = 0; i < n_part; ++i) {
      Particle *p = dens_sorted.at(i);
      dX += p->GetX() * p->GetM();
      dV += p->GetV() * p->GetM();
      total_mass += p->GetM();
    }
    dX /= total_mass;
    dV /= total_mass;
    appendage += "densest";
  } else {
    if (posCenter.Norm() == 0.0) {
      dX = sink[sinkIndex]->GetX();
      appendage += std::to_string(sinkIndex);
    } else {
      dX = posCenter;
      appendage += mParams->GetString("CENTER_LABEL");
    }
    if (dX.z == 0.0) {
      dX.z = sink[sinkIndex]->GetX().z;
    }
  }

  // TODO: Instead of using R, maybe replace all with GetX().Norm().
  // TODO: Set new velocity for all cases!
  for (int i = 0; i < part.size(); ++i) {
    Vec3 newX = part.at(i)->GetX() - dX;
    Vec3 newV = part.at(i)->GetV() - dV;
    part.at(i)->SetX(newX);
    part.at(i)->SetR(part.at(i)->GetX().Norm());
    part.at(i)->SetV(newV);
  }

  for (int i = 0; i < sink.size(); ++i) {
    Vec3 newX = sink[i]->GetX() - dX;
    sink[i]->SetX(newX);
    sink[i]->SetR(sink[i]->GetX().Norm());
  }
  std::cout << "   " << file->GetNameData().id << " centering R "
            << sink[0]->GetX().Norm() << "\n";

  file->SetParticles(part);
  file->SetSinks(sink);
  file->SetNameDataAppend(appendage);
}

void DiscAnalyser::FindOuterRadius(SnapshotFile *file) {
  std::vector<Particle *> part = file->GetParticles();

  // Find the total gas mass.
  double total_mass = part.size() * part[0]->GetM();

  // Sort by radius.
  std::sort(part.begin(), part.end(),
            [](Particle *a, Particle *b) { return b->GetR() > a->GetR(); });

  // Find radius encompassing [90%, 95%, 99%] of the gas mass.
  for (int i = 0; i < 3; ++i) {
    double accum_mass = 0.0f;
    double threshold = total_mass * ROUT_PERCS[i];
    for (int j = 0; j < part.size(); ++j) {
      Particle *p = part[j];
      accum_mass += p->GetM();
      if (accum_mass >= threshold) {
        file->SetOuterRadius(p->GetX().Norm(), i);
        break;
      }
    }
  }
}