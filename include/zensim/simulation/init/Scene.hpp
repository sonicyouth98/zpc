#pragma once
#include "Structure.hpp"
#include "Structurefree.hpp"
#include "zensim/geometry/LevelSet.h"
#include "zensim/math/Vec.h"
#include "zensim/physics/ConstitutiveModel.hpp"
#include "zensim/tpls/magic_enum.hpp"
#include "zensim/types/BuilderBase.hpp"

namespace zs {

  struct SceneBuilder;
  /// scene setup
  struct Scene {
    enum struct model_e : char { None = 0, LevelSet, Particle, Mesh, Nodes, Grid };
    static constexpr int numConstitutiveModels
        = magic_enum::enum_integer(constitutive_model_e::NumConstitutiveModels);

    // std::vector<GeneralLevelSet> levelsets;
    std::vector<GeneralParticles> particles;
    std::vector<GeneralMesh> meshes;
    std::vector<GeneralNodes> nodes;
    std::vector<GeneralGridBlocks> grids;
    /// (constitutive model [elasticity, plasticity], geometry type, local model index)
    std::vector<std::tuple<ConstitutiveModelConfig, model_e, std::size_t>> models;
    static SceneBuilder create();
  };

  struct BuilderForSceneParticle;
  struct BuilderForSceneMesh;
  struct BuilderForScene : BuilderFor<Scene> {
    explicit BuilderForScene(Scene &scene) : BuilderFor<Scene>{scene} {}
    BuilderForSceneParticle particle();
    BuilderForSceneMesh mesh();
  };

  struct SceneBuilder : BuilderForScene {
    SceneBuilder() : BuilderForScene{_scene} {}

  protected:
    Scene _scene;
  };

  struct BuilderForSceneParticle : BuilderForScene {
    explicit BuilderForSceneParticle(Scene &scene) : BuilderForScene{scene} {}
    /// particle positions
    BuilderForSceneParticle &addParticles(std::string fn, float dx, float ppc);
    BuilderForSceneParticle &addCuboid(std::vector<float> mi, std::vector<float> ma, float dx,
                                       float ppc);
    BuilderForSceneParticle &addCube(std::vector<float> c, float len, float dx, float ppc);
    BuilderForSceneParticle &addSphere(std::vector<float> c, float r, float dx, float ppc);

    /// constitutive models
    BuilderForSceneParticle &setConstitutiveModel(constitutive_model_e);
#define SETUP_CONSTITUTIVE_PARAMETER(NAME, ATTRIB)                                         \
  BuilderForSceneParticle &NAME(float v) {                                                 \
    /*match([v](auto &model) { model.ATTRIB = v; }, [v](auto &model) {})(config); */       \
    match([](...) {},                                                                      \
          [v](auto &model) -> std::enable_if_t<is_same_v<decltype(model.ATTRIB), float>> { \
            model.ATTRIB = v;                                                              \
          })(config);                                                                      \
    return *this;                                                                          \
  }
    /// function name, data member name
    SETUP_CONSTITUTIVE_PARAMETER(density, rho);
    SETUP_CONSTITUTIVE_PARAMETER(volume, volume);
    SETUP_CONSTITUTIVE_PARAMETER(bulk, bulk);
    SETUP_CONSTITUTIVE_PARAMETER(gamma, gamma);
    SETUP_CONSTITUTIVE_PARAMETER(viscosity, viscosity);
    SETUP_CONSTITUTIVE_PARAMETER(youngsModulus, E);
    SETUP_CONSTITUTIVE_PARAMETER(poissonRatio, nu);
    SETUP_CONSTITUTIVE_PARAMETER(logjp0, logJp0);
    SETUP_CONSTITUTIVE_PARAMETER(frictionAngle, fa);
    SETUP_CONSTITUTIVE_PARAMETER(xi, xi);
    SETUP_CONSTITUTIVE_PARAMETER(beta, beta);
    SETUP_CONSTITUTIVE_PARAMETER(yieldSurface, yieldSurface);
    SETUP_CONSTITUTIVE_PARAMETER(cohesion, cohesion);

    /// push to scene
    BuilderForSceneParticle &commit(MemoryHandle dst);
    /// check build status
    BuilderForSceneParticle &output(std::string fn);

    using ParticleModel = std::vector<std::array<float, 3>>;

  protected:
    std::vector<ParticleModel> particlePositions;
    ConstitutiveModelConfig config{EquationOfStateConfig{}};
  };
  struct BuilderForSceneMesh : BuilderForScene {
    explicit BuilderForSceneMesh(Scene &scene) : BuilderForScene{scene} {}
    BuilderForSceneMesh &addMesh(std::string fn) { return *this; }

    // std::vector<Mesh> meshes;
    ConstitutiveModelConfig config{EquationOfStateConfig{}};
  };

  /// simulator setup
  struct SimOptionsBuilder;
  struct SimOptions {
    float fps;
    float dt;
    float dx;
    static SimOptionsBuilder create();
  };

  struct BuilderForSimOptions : BuilderFor<SimOptions> {
    BuilderForSimOptions(SimOptions &simOptions) : BuilderFor<SimOptions>{simOptions} {}
    BuilderForSimOptions &fps(float v) {
      this->object.fps = v;
      return *this;
    }
    BuilderForSimOptions &dt(float v) {
      this->object.dt = v;
      return *this;
    }
    BuilderForSimOptions &dx(float v) {
      this->object.dx = v;
      return *this;
    }
  };
  struct SimOptionsBuilder : BuilderForSimOptions {
    SimOptionsBuilder() : BuilderForSimOptions{_simOptions} {}

  protected:
    SimOptions _simOptions;
  };

}  // namespace zs