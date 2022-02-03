/*
 * Copyright (C) 2022 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "Wavefield.hh"
#include "WaveVisual.hh"

#include <list>
#include <chrono>
#include <mutex>
#include <vector>
#include <string>

#include <ignition/common/Profiler.hh>
#include <ignition/plugin/Register.hh>
#include <ignition/rendering/Material.hh>
#include <ignition/rendering/RenderingIface.hh>
#include <ignition/rendering/Scene.hh>
#include <ignition/rendering/ShaderParams.hh>
#include <ignition/rendering/Visual.hh>

#include <sdf/Element.hh>

#include "ignition/gazebo/components/Name.hh"
#include "ignition/gazebo/components/SourceFilePath.hh"
#include "ignition/gazebo/rendering/Events.hh"
#include "ignition/gazebo/rendering/RenderUtil.hh"
#include "ignition/gazebo/Util.hh"

using namespace ignition;
using namespace gazebo;
using namespace systems;

class ignition::gazebo::systems::WaveVisualPrivate
{
  /// \brief Path to vertex shader
  public: std::string vertexShaderUri;

  /// \brief Path to fragment shader
  public: std::string fragmentShaderUri;

  /// \brief Mutex to protect sim time updates.
  public: std::mutex mutex;

  /// \brief Connection to pre-render event callback
  public: ignition::common::ConnectionPtr connection{nullptr};

  /// \brief Name of visual this plugin is attached to
  public: std::string visualName;

  /// \brief Pointer to visual
  public: rendering::VisualPtr visual;

  /// \brief Material used by this visual
  public: rendering::MaterialPtr material;

  /// \brief Pointer to scene
  public: rendering::ScenePtr scene;

  /// \brief Entity id of the visual
  public: Entity entity = kNullEntity;

  /// \brief Current sim time
  public: std::chrono::steady_clock::duration currentSimTime;

  /// \brief Path to model
  public: std::string modelPath;

  /// \brief Wavefield for computing wave params
  public: Wavefield wavefield;

  /// \brief Indicate whether the shader params have been set or not
  public: bool paramsSet = false;

  /// \brief All rendering operations must happen within this call
  public: void OnUpdate();
};

/////////////////////////////////////////////////
WaveVisual::WaveVisual()
    : System(), dataPtr(std::make_unique<WaveVisualPrivate>())
{
}

/////////////////////////////////////////////////
WaveVisual::~WaveVisual()
{
}

/////////////////////////////////////////////////
void WaveVisual::Configure(const Entity &_entity,
               const std::shared_ptr<const sdf::Element> &_sdf,
               EntityComponentManager &_ecm,
               EventManager &_eventMgr)
{
  IGN_PROFILE("WaveVisual::Configure");
  // Ugly, but needed because the sdf::Element::GetElement is not a const
  // function and _sdf is a const shared pointer to a const sdf::Element.
  auto sdf = const_cast<sdf::Element *>(_sdf.get());

  if (!sdf->HasElement("wavefield"))
  {
    ignerr << "<wavefield> parameter is missing " << std::endl;
    return;
  }
  if (!sdf->HasElement("shader"))
  {
    ignerr << "<shader> parameter is missing " << std::endl;
    return;
  }

  this->dataPtr->wavefield.Load(_sdf);

  if (this->dataPtr->modelPath.empty())
  {
    auto modelEntity = topLevelModel(_entity, _ecm);
    this->dataPtr->modelPath =
        _ecm.ComponentData<components::SourceFilePath>(modelEntity).value();
  }

  // parse path to shaders
  sdf::ElementPtr shaderElem = sdf->GetElement("shader");
  if (!shaderElem->HasElement("vertex") ||
      !shaderElem->HasElement("fragment"))
  {
    ignerr << "<shader> must have <vertex> and <fragment> sdf elements"
           << std::endl;
  }
  else
  {
    sdf::ElementPtr vertexElem = shaderElem->GetElement("vertex");
    this->dataPtr->vertexShaderUri = common::findFile(
        asFullPath(vertexElem->Get<std::string>(), this->dataPtr->modelPath));
    sdf::ElementPtr fragmentElem = shaderElem->GetElement("fragment");
    this->dataPtr->fragmentShaderUri = common::findFile(
        asFullPath(fragmentElem->Get<std::string>(), this->dataPtr->modelPath));
  }

  this->dataPtr->entity = _entity;
  auto nameComp = _ecm.Component<components::Name>(_entity);
  this->dataPtr->visualName = nameComp->Data();

  // connect to the SceneUpdate event
  // the callback is executed in the rendering thread so do all
  // rendering operations in that thread
  this->dataPtr->connection =
      _eventMgr.Connect<ignition::gazebo::events::SceneUpdate>(
      std::bind(&WaveVisualPrivate::OnUpdate, this->dataPtr.get()));
}

//////////////////////////////////////////////////
void WaveVisual::PreUpdate(
  const ignition::gazebo::UpdateInfo &_info,
  ignition::gazebo::EntityComponentManager &)
{
  IGN_PROFILE("WaveVisual::PreUpdate");
  std::lock_guard<std::mutex> lock(this->dataPtr->mutex);
  this->dataPtr->currentSimTime = _info.simTime;
}

//////////////////////////////////////////////////
void WaveVisualPrivate::OnUpdate()
{
  std::lock_guard<std::mutex> lock(this->mutex);
  if (this->visualName.empty())
    return;

  if (!this->scene)
    this->scene = rendering::sceneFromFirstRenderEngine();

  if (!this->scene)
    return;

  if (!this->visual)
  {
    // this does a breadth first search for visual with the entity id
    // \todo(anyone) provide a helper function in RenderUtil to search for
    // visual by entity id?
    auto rootVis = scene->RootVisual();
    std::list<rendering::NodePtr> nodes;
    nodes.push_back(rootVis);
    while (!nodes.empty())
    {
      auto n = nodes.front();
      nodes.pop_front();
      if (n && n->HasUserData("gazebo-entity"))
      {
        // RenderUti stores gazebo-entity user data as int
        // \todo(anyone) Change this to uint64_t in Ignition H?
        auto variant = n->UserData("gazebo-entity");
        const int *value = std::get_if<int>(&variant);
        if (value && *value == static_cast<int>(this->entity))
        {
          this->visual = std::dynamic_pointer_cast<rendering::Visual>(n);
          break;
        }
      }
      for (unsigned int i = 0; i < n->ChildCount(); ++i)
        nodes.push_back(n->ChildByIndex(i));
    }
  }

  if (!this->visual)
    return;

  // get the material and set shaders
  if (!this->material)
  {
    auto mat = scene->CreateMaterial();
    mat->SetVertexShader(this->vertexShaderUri);
    mat->SetFragmentShader(this->fragmentShaderUri);
    this->visual->SetMaterial(mat);
    scene->DestroyMaterial(mat);
    this->material = this->visual->Material();
  }

  if (!this->material)
    return;

  if (!this->paramsSet)
  {
    auto vsParams = this->material->VertexShaderParams();

    (*vsParams)["Nwaves"] = static_cast<int>(this->wavefield.Number());
    (*vsParams)["rescale"] = 0.5f;

    float bumpScale[2] = {25.0f, 25.0f};
    (*vsParams)["bumpScale"].InitializeBuffer(2);
    (*vsParams)["bumpScale"].UpdateBuffer(bumpScale);

    float bumpSpeed[2] = {0.01f, 0.01f};
    (*vsParams)["bumpSpeed"].InitializeBuffer(2);
    (*vsParams)["bumpSpeed"].UpdateBuffer(bumpSpeed);

    float amplitudeV[3] = {
        static_cast<float>(this->wavefield.Amplitude_V()[0]),
        static_cast<float>(this->wavefield.Amplitude_V()[1]),
        static_cast<float>(this->wavefield.Amplitude_V()[2])};
    (*vsParams)["amplitude"].InitializeBuffer(3);
    (*vsParams)["amplitude"].UpdateBuffer(amplitudeV);

    float wavenumberV[3] = {
        static_cast<float>(this->wavefield.Wavenumber_V()[0]),
        static_cast<float>(this->wavefield.Wavenumber_V()[1]),
        static_cast<float>(this->wavefield.Wavenumber_V()[2])};
    (*vsParams)["wavenumber"].InitializeBuffer(3);
    (*vsParams)["wavenumber"].UpdateBuffer(wavenumberV);

    float omegaV[3] = {
        static_cast<float>(this->wavefield.AngularFrequency_V()[0]),
        static_cast<float>(this->wavefield.AngularFrequency_V()[1]),
        static_cast<float>(this->wavefield.AngularFrequency_V()[2])};
    (*vsParams)["omega"].InitializeBuffer(3);
    (*vsParams)["omega"].UpdateBuffer(omegaV);

    auto directions0 = this->wavefield.Direction_V()[0];
    float dir0[2] = {
        static_cast<float>(directions0.X()),
        static_cast<float>(directions0.Y())};
    (*vsParams)["dir0"].InitializeBuffer(2);
    (*vsParams)["dir0"].UpdateBuffer(dir0);

    auto directions1 = this->wavefield.Direction_V()[1];
    float dir1[2] = {
        static_cast<float>(directions1.X()),
        static_cast<float>(directions1.Y())};
    (*vsParams)["dir1"].InitializeBuffer(2);
    (*vsParams)["dir1"].UpdateBuffer(dir1);

    auto directions2 = this->wavefield.Direction_V()[2];
    float dir2[2] = {
        static_cast<float>(directions2.X()),
        static_cast<float>(directions2.Y())};
    (*vsParams)["dir2"].InitializeBuffer(2);
    (*vsParams)["dir2"].UpdateBuffer(dir2);

    float steepnessV[3] = {
        static_cast<float>(this->wavefield.Steepness_V()[0]),
        static_cast<float>(this->wavefield.Steepness_V()[1]),
        static_cast<float>(this->wavefield.Steepness_V()[2])};
    (*vsParams)["steepness"].InitializeBuffer(3);
    (*vsParams)["steepness"].UpdateBuffer(steepnessV);

    float tau = this->wavefield.Tau();
    (*vsParams)["tau"] = tau;

    // camera_position_object_space is a constant defined by ogre.
    (*vsParams)["camera_position_object_space"] = 1;

    // set fragment shader params
    auto fsParams = this->material->FragmentShaderParams();

    float hdrMultiplier = 0.4f;
    (*fsParams)["hdrMultiplier"] = hdrMultiplier;

    float fresnelPower = 5.0f;
    (*fsParams)["fresnelPower"] = fresnelPower;

    float shallowColor[4] = {0.0f, 0.1f, 0.3f, 1.0f};
    (*fsParams)["shallowColor"].InitializeBuffer(4);
    (*fsParams)["shallowColor"].UpdateBuffer(shallowColor);

    float deepColor[4] = {0.0f, 0.05f, 0.2f, 1.0f};
    (*fsParams)["deepColor"].InitializeBuffer(4);
    (*fsParams)["deepColor"].UpdateBuffer(deepColor);

    std::string bumpMapPath = common::findFile(
        asFullPath("materials/textures/wave_normals.dds",
        this->modelPath));
    (*fsParams)["bumpMap"].SetTexture(bumpMapPath,
        rendering::ShaderParam::ParamType::PARAM_TEXTURE);

    std::string cubeMapPath = common::findFile(
        asFullPath("materials/textures/skybox_lowres.dds", this->modelPath));
    (*fsParams)["cubeMap"].SetTexture(cubeMapPath,
        rendering::ShaderParam::ParamType::PARAM_TEXTURE_CUBE, 1u);
    this->paramsSet = true;
  }

  // time variables need to be updated every iteration
  float floatValue = (std::chrono::duration_cast<std::chrono::nanoseconds>(
      this->currentSimTime).count()) * 1e-9;
  rendering::ShaderParamsPtr params;
  params = this->material->VertexShaderParams();
  (*params)["t"] = floatValue;
}

IGNITION_ADD_PLUGIN(WaveVisual,
                    ignition::gazebo::System,
                    WaveVisual::ISystemConfigure,
                    WaveVisual::ISystemPreUpdate)

IGNITION_ADD_PLUGIN_ALIAS(WaveVisual,
  "ignition::gazebo::systems::WaveVisual")
