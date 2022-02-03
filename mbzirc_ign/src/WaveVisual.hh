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

#ifndef IGNITION_GAZEBO_SYSTEMS_WAVEVISUAL_HH_
#define IGNITION_GAZEBO_SYSTEMS_WAVEVISUAL_HH_

#include <memory>

#include "ignition/gazebo/System.hh"

namespace ignition
{
namespace gazebo
{
// Inline bracket to help doxygen filtering.
inline namespace IGNITION_GAZEBO_VERSION_NAMESPACE {
namespace systems
{
  // Forward declaration
  class WaveVisualPrivate;

  /// \brief A plugin for setting shaders to a visual and its params
  ///
  /// Plugin parameters:
  ///
  /// <shader>
  ///   <vertex>   Path to vertex program
  ///   <fragment> Path to fragment program
  /// <param>      Shader parameter - can be repeated within plugin SDF element
  ///   <name>     Name of uniform variable bound to the shader
  ///   <shader>   Type of shader, i.e. vertex, fragment
  ///   <type>     Variable type: float, int, float_array, int_array
  ///   <value>    Value to set the shader parameter to. The vallue string can
  ///              be an int, float, or a space delimited array of ints or
  ///              floats. It can also be 'TIME', in which case the value will
  ///              be bound to sim time.
  ///
  /// Example usage:
  ///
  /// \verbatim
  ///     <plugin filename="ignition-gazebo-wave-visual-system"
  ///             name="ignition::gazebo::systems::WaveVisual">
  ///        <shader>
  ///          <vertex>materials/my_vs.glsl</vertex>
  ///          <fragment>materials/my_fs.glsl</fragment>
  ///        </shader>
  ///        <!-- Sets a fragment shader variable named "ambient" to red -->
  ///        <param>
  ///          <name>ambient</name>
  ///          <shader>fragment</shader>
  ///          <type>float_array</type>
  ///          <value>1.0 0.0 0.0 1.0</value>
  ///        </param>
  ///    </plugin>
  /// \endverbatim
  class WaveVisual
      : public System,
        public ISystemConfigure,
        public ISystemPreUpdate
  {
    /// \brief Constructor
    public: WaveVisual();

    /// \brief Destructor
    public: ~WaveVisual() override;

    // Documentation inherited
    public: void Configure(const Entity &_entity,
                           const std::shared_ptr<const sdf::Element> &_sdf,
                           EntityComponentManager &_ecm,
                           EventManager &_eventMgr) final;

    /// Documentation inherited
    public: void PreUpdate(
                const ignition::gazebo::UpdateInfo &_info,
                ignition::gazebo::EntityComponentManager &_ecm) override;

    /// \brief Private data pointer
    private: std::unique_ptr<WaveVisualPrivate> dataPtr;
  };
  }
}
}
}

#endif
