#version 320 es
 /* Copyright (C) 2020 - 2022 OPPO. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

precision mediump float;

layout(location = 0) out vec2 v_uv;

const vec4 v[] = vec4[](
    vec4(-1., -1.,  0., 1.),
    vec4( 3., -1.,  0., -1.),
    vec4(-1.,  3.,  2., 1.));

void main()
{   
    gl_Position = vec4(v[gl_VertexIndex].xy, 0., 1.);
    v_uv = vec2(v[gl_VertexIndex].z, v[gl_VertexIndex].w);
}