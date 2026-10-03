# W4M rendering

Part of the W4M map (index, tools, tags: [README.md](README.md)).

## 8. Rendering
Conf: data = read from CG/ or exe strings; disasm = code; assumed = inferred.

### 1. CG shaders (data)
| File | Role | Entry points | Key uniforms / inputs |
|---|---|---|---|
| FixedFunction.cg | GL fixed-function emulation for all meshes | 48 vertex: FFVertexMain{,Tex}{,LitPoint,LitDirectional}{,Skinned}{,Col}{,Lighting} (combinatorial); fragment: FFFragmentMain, FFFragmentMainTex, FFFragmentMainLit, FFFragmentMainTexLit, FFFragmentMain{,Tex}{,Lit}Col; helper CalculateLighting | model/view/projection, texTransform, materialMatrix, BlendMat[35] (register c8, skinning palette), numBonesPerVertex, lightPosition (model space), lightDirection, lightAmbient/Diffuse/SpecularCol, viewProjection. "Lighting" variants = position-only pass (assumed: lighting/shadow colour pass) |
| Landscape.cg | Terrain (heightmap chunks) with shadow map | LandscapeVertexMain, LandscapeFragmentMain; helpers GetShadowScale, GetInShadow | VS: model, view, projection, globalLightDir, texTransform, shadowMatrix; in POSITION/NORMAL/TEXCOORD0/COLOR; out TEX4=shadow-space pos. FS: texture0, shadowMap, shadowSize(xy=map size), globalDiffuse/Ambient/Specular/Fresnel. Shadow PCF: 3x3 weighted (0.075 corner / 0.124 edge / 0.204 centre, offset 0.5 texel) on one path, 2x2 bilinear compare or 5-tap cross on others (platform #if). Outside [0,1] = lit. Lighting skipped if shadowScale<=0.01 (diffuse/spec 0). Comment: specular/fresnel consts hard-coded, shared across all landscapes |
| water.cg | Water plane | WaterVertexMain, WaterVertexMainLighting (pos-only), WaterFragmentMain, OldWaterFragmentMain; helpers Panner, FromCubeMap, FromNormalMap | VS: modelViewProjection, modelView, model, cameraPos, textureScale. FS: texture0 diffuse, texture1 normal, texture2 environment spherical map (old: cubemap), pausedTime, combinedWaterParams (float4x4): [0].x reflection_contrast, [0].y reflection_strength, [0].z specular_power, [1].xyz waterNormalIntensity, [2].xyz waterNormalIntensity1, [3].x specular_contrast, [3].y specularFadeScale, [3].z nearOpacity, [3].w substractColourScale. 3 scrolled normal layers: t=pausedTime*0.5; UV*5 + t*(-0.3,0.6); UV*-0.2 + t*(0,0.2); UV*-0.75 + t*(-0.1,-0.2). reflection = pow(env, contrast)*strength; spec = saturate(pow(env.r, specular_power)*specular_contrast) |
| PostProcess.cg | Full-screen passes | VertexMain (shared VS); FS: Copy1x1/1x2/2x2/2x4/4x4 (box downsample, SSAA resolve; texCoord.zw = texel size), CopyWithLuminance (alpha = Rec601 luma .299/.587/.114, for FXAA), CopyFxaa (FXAA 3.9, FXAA_PC + GLSL_120, uniform rcpFrame), CopySepia{1x1..4x4}, CopyFxaaSepia, Silhouette (console), Silhouette_PC, Outline, RecombineWorms | Sepia: luma (0.3,0.59,0.11), lerp(col, luma*sepiaColour.rgb, sepiaColour.a) (vertex COLOR0 carries colour+weight). Silhouette fill colour (0.25,0.25,0.25,0.5): occluded worm pixels blended 50% grey. Outline: 4 diagonal depth taps at +-1 texel of worm depth tex; any tap >=1 (empty) -> outlineColour (COLOR0, alpha blend), else discard. RecombineWorms: lerp(scene, worm colour, wormOpacity.a), depth=min |
| XenonVideo.cg | Xbox360 video YUV->RGB (unused on PC, assumed) | XenonVideoVertexMain, XenonVideoFragmentMain | texture0/1/2 = Y/U/V, modelViewProjection |
| Fxaa3_9.h | NVIDIA FXAA 3.9 header, included by PostProcess.cg | FxaaPixelShader | - |

### 2. Post-process (data: exe strings)
- Classes (rtti): BasePostProcess, IXPostProcess (interface), PCPostProcess (impl, source PCPostProcess.cpp, has InitialiseGlFunctions), NullPostProcess (assumed: used with /NOPOSTPROCESS).
- Render bins: OutlinedWormsPreProcess, OutlinedWorms1, OutlinedWorms2, OutlinedWorms3, OutlinedWormsPostProcess.
- Logs: "PCPostProcess: SSAA now set to", "... with FXAA", "NVidia FXAA enabled", "Fxaa is not available: ", "Error - CopyFxaa not found".
- Sepia tweak keys: "Sepia", "Sepia.LerpWeight", "Sepia.Color".
- Scene-graph effect classes also present: XBloomShape, XFocusBlurShape, XBlurEffect (+ "BlurSeparation" field). No bloom/DOF shader in CG/ -> assumed unused / fixed-function path on PC.
- Extra shader entries referenced by exe (data): Landscape.cg also has LightingLandscapeVertexMain/FragmentMain and LightingHeightMapVertexMain/FragmentMain (position-only lighting/shadow passes). Exe refs Landscape.cg in 004480f0, 00460560, 004d4d70; water entries in 0048b460, 0048bc00 (WaterCgGraphicEntity), 004d4d70, 004d6430.
- PCPostProcess functions (disasm, via PCPostProcess.cpp refs): InitialiseGlFunctions 0061c600; program creation / CopyFxaa lookup 0061d050; uses "shadowMap" 0061d6b0; Initialise + Sepia.Color/LerpWeight read 0061f190; SSAA set/log 0061f7a0; others 0061cb10 0061ce00 0061cf00 0061dcb0 0061e0e0. Vtables: PCPostProcess 0086c7d4/0086c7f4, BasePostProcess 0086c07c/0086c09c, NullPostProcess 0086ca50.

#### Render bins (data: pointer table at 0091e2b8, 69 entries, index = draw order)
0 FESkybox1/Particle0, 1 FESkybox2/Skybox1, 2 FESkybox3/Skybox2, 3 FEWater/Skybox3, 4 FEWater2/Landbase/Land1, 5 FELensFlare/Land2, 6 LandShadow, 7 AfterLandHUD, 8 3D, 9 WaterBlend, 10 Water, 11 ParticleUnderWater, 12-14 Water2-4, 15 WaterRipple, 16 DetailObjects, 17 LandFringe, 18 OutlinedWormsPreProcess, 19-21 OutlinedWorms1-3, 22 OutlinedWormsPostProcess, 23-27 Particle1-5, 28 3DTextBack, 29 3DText, 30 LensFlare, 31 FirstPersonWeapon, 32 MenuBack, 33 WhiteOut, 34 HUDSniper, 35 HUD0/HUDFirst/HUDBack, 36 HUD1, 37 HUD2/HUDMiddle, 38 HUD3, 39 HUD4/HUDFront, 40-62 HUD5-27, 63 HUDLast/HUDMouse, 64 EFMVBorders, 65 EFMVBriefingText, 66-68 Loading0-2.
Implication (assumed): worms are drawn after land/water/details into OutlinedWorms bins, silhouette+outline composited in PostProcess bin, then particles (Particle1-5) on top, then HUD.

#### Command-line switches (data strings; flag effects disasm in 004d95c0, config object *0x0095a100)
| Switch | Effect |
|---|---|
| /SSAA N | N in 2..16 switch; sets cfg+0x6c/+0x70 = X/Y factors (2 -> 1x2, 8 -> 2x4; 4 -> 2x2, 16 -> 4x4 assumed) matching Copy1x2..Copy4x4 resolve shaders |
| /NOSSAA, /FXAA, /DISABLEHARDWAREAA | AA toggles ("NVidia FXAA enabled") |
| /SEPIA | calls fn ptr ds:0x95a0e8(1); "Activate Sepia mode" |
| /WIREFRAME | cfg+0x9b |= 0x20 |
| /CREATESHADOWCOLOURMAP | cfg+0x9d |= 0x80 ("colour texture for the lighting pass") |
| /SHADOWMAP N | cfg+0x88 (WORD) = shadow map size |
| /NOWORMOUTLINES | cfg+0x9c &= ~0x20 (outlines on by default) |
| /AMBIENTOCCLUSION | cfg+0x9c |= 0x10 |
| /NOPOSTPROCESS | cfg+0x9c |= 0x08 |
| others (non-render) | /LOG /RUN /CRC /DONTOPTIMIZEATTRIBUTES /IMPORTTGAS /EXPORTTGAS /STARTUP /NOHARDWARESOUND /NET_LOG /AITEST /NOATTRACT /NOFRONTEND /TRIGGERSINVISIBLE /ALTJUMP /SELFTEST /NEW_CONTROL_BOX /GAMEOVERMENU /STARTMENU /MENU /WEATHER /LEVELNAME /DATABANK /SCRIPT /TIMEOFDAY /THEME /LEVEL /SHUTUP /NOCAMERALOAD /DELETECLOUDSAVE /NOSOUND /NOMUSIC /SHRINKHUD /ZIP /GAMETIMESTAMP /GAME /NAME /JOIN /HOST /ENABLEPORTFORWARDING |

### 3. Particles (data: PARTTWK.XOM via xom.py)
- Containers: 202 EffectDetailsContainer, 874 ParticleEmitterContainer, 24 XUintResourceDetails (TwkEdVer.* editor versions, all 12), 1 XDataBank; 1075 XContainerResourceDetails (name -> container index).
- EffectDetailsContainer = only `EffectNames: [emitter names]` (e.g. 3 emitters ParticleExplosionRing, ParticleFireFlash, ParticleExplosion). Emitters per effect: 0..14, mode 2 (dist 1:34 2:54 3:34 4:18 5:15 6:11 7:7 8:10 9:4 10:2 11:2 12:1 14:2 0:8).
- Naming: effects mostly WXP_* (97), Part* (26), WXPL_ (6, level/scenery: twister, tower storm, seagull, worm ghost), WXPF_ (6, assumed front-end), Weapon* flashes; emitters WXP 425, Part 84, WXPL 34, WXPF 21, R_ (14, R_Explosion_*), AM, Sprite, Splash, W_ (W_Explosion_*), Boomf.
- ParticleEmitterContainer: 98 fields. Groups: Emitter* (Type, Acceleration(+Randomise), IsAttachedToLand, IsOfInterest, IsLaunchedFromWeapon, LifeTime(+Rand), MaxParticles, NumCollide, NumSpawn(+"Radnomise" typo), OriginOffset(+Rand), ParticleExpireFX, ParticleFX, SameDirectionAsWorm, SoundFX, SoundFXVolume, SpawnFreq(+"Ransomise"), SpawnSizeVelocity, StartDelay, Velocity(+Rand)); Particle* (Acceleration, Alpha, AlphaFadeIn, AlphaVelocity(+Delay), AlternateAcceleration N/S, AnimationFrame/Speed(+Rand), Attractor(+IsActive), CanEnterWater, Collision* (Freq, ImmuneTime, MinAlpha, "Collison"Radius(+Offset,+Velocity), ShowDebug, WormDamage/Impulse/ImpulseY/Poison Magnitude, WormType), Color[] + ColorBand[] + NumColors, ExpireShake(+Length,+Magnitude), IsAlternateAcceleration, IsEffectedByWind, IsSpiral, IsUnderWaterEffect, LandCollideType, Life(+Rand) ms, Mass, NumFrames, Orientation(+Velocity,+Rand), Size vec2 (+Rand, Velocity, VelocityDelay, FinalSizeScale, SizeFadeIn(+Delay), SizeOriginIsCenterPoint), RenderScene, Spiral* (Radius, RadiusVelocity, RadiusSizeVelocity), Velocity(+Rand, IsNormalised)); plus Comment, SpriteSet (e.g. Particle.WXSprite4), MeshSet[], MeshAnimNodeName.
- No blend field on emitters: blend comes from sprite set texture (Particle.Additive1-3 sprites exist) / XBlendModeGL with kBlendFactor{Zero,One,SrcColor,OneMinusSrcColor,DestColor,OneMinusDestColor,SrcAlpha,OneMinusSrcAlpha,DestAlpha,OneMinusDestAlpha,SrcAlphaSaturate,MinusOne} (data; mapping assumed).
- Enums (values from the field descriptors, `pe.py schema '^ParticleEmitterContainer$'`) [disasm]:
  | Field | Enum | Values | Usage in PARTTWK |
  |---|---|---|---|
  | EmitterType | ParticleTypeEnum | kNormal 0, kSnow 1, kRain 2, kTrail 3 | 0:838 1:2 2:4 3:30 |
  | ParticleRenderScene | ParticleSceneEnum | kPS_Default 0, kPS_Scene1..5 = 1..5. Bin (0x5bb510): Scene*n* -> bin n + 22 = Particle*n* (23-27, 0x5bb57d); Default keeps the drawable's own bin (vfunc 0x44, 0x5bb51e) [disasm] | 0:713 1:101 2:14 3:24 4:7 5:15 |
  | ParticleLandCollideType | LandCollideEnum | kLC_None 0, kLC_Bounce 1, kLC_Expire 2, kLC_StopMoving 3, kLC_StopMovingAndAttach 4 (order data: `pe.py schema` enum list) | 0:666 1:202 2:2 3:4 |
  | ParticleCollisionWormType | WormCollideEnum | kWC_Standard, kWC_Expire, kWC_Lightside, kWC_Darkside (+ WormCollideResponseEnum kWC_Default/StealInventory/FloatAway) | 0:794 1:76 2:2 3:2 |
- Stats: MeshSet non-empty 149; NumColors 0..5; spiral 55; wind 51; underwater 8; attached-to-land 5; attractor never active; chained ParticleFX 98, ExpireFX 11, SoundFX 152. Top sprite sets: none(182), Particle.WXSprite4 (176), WXSprite1 (85), WhitePuff (50), ElectricSpark (37), WXSprite7 (29).
- **Particle maths** (Particle.cpp, CParticle vtable 0x860734) [disasm]: t = particle age in ms. Setup 0x5b98e0 (r uniform in [0,1)): v0 = (V + (2r−1)·Vrand)·0.01 units/ms (V is per 100 ms; IsNormalised: unit vector × (V.x + Vrand.x)); a = (A + (2r−1)·Arand)·1e-4 units/ms². Position 0x5b7450: p = p0 + v0·t + 0.5·Mass·(a + wind)·t² (Mass scales the acceleration; wind only with IsEffectedByWind). IsAlternateAcceleration: no gravity, p = p0 + v0·(N − 1/(S·t + 1/N)) (N, S = AlternateAccelerationN/S), plus v0.y·t on y. Size 0x5b6f40: fade-in, then S until SizeVelocityDelay, then linear to S·FinalSizeScale, or with FinalSizeScale 0 and SizeVelocity < 0 a linear shrink to 0 at end of life. Alpha 0x5b7660: AlphaVelocity < 0 = linear fade to 0 at end of life (magnitude ignored). Colour 0x5b77c0: NumColors 0 white, 1 Color[0], 2 lerp over life, ≥3 piecewise by ColorBand. Emitter: SpawnFreq = period in ms (0 = one burst), StartDelay ms, pool capped by MaxParticles. kTrail (3) makes one TrailGraphicEntity ribbon per particle (0x5c2580, 24 divisions, "A,B" sprite set = ribbon texture, head sprite [assumed]).
- **Sprite set blends** (Bundl10: descriptor → XGroup → XShape → shader render states) [data]: WXSprite1 and Whiteout additive (SrcAlpha, One; #1232); WXSprite4/5/7/26/30 and Fade alpha (#1231); TrailSprite_B/R/W alpha (#1230), but those images have no alpha channel (black ground).
- **Victory fireworks** (GameOverLogicEntity update 0x4ff8d0, every 20 ms) [disasm]: 4000 ms after the end, the orbit camera starts (0x4ff790; Script.NoOrbitCamera skips the fireworks). Then, for 5000 ms (15000 when 0x5a6350), each tick fires with chance 1/40 (one per 800 ms on average) `WXPF_Firework<1 + rand%5>` at x, z = Land.Center ± Land.Radius/2, y = Land.MaxHeight + r·30 units (0x5c1410). Any input ends the show. Compositions (PARTTWK): 1 cyan glows + StarburstTrailsA/B + BlueTrails_2; 2 orange glows + 2×RedTrails_1 + Exploder_1; 3 orange glows + Exploder_1/2 + delayed 10 m glows + delayed whiteout; 4 green glows + ExploderGreen (GreenTrail1 child puffs); 5 orange glows + 4×RedTrailsLong. Each also has WXPF_Whiteout (400×300 units, alpha 0.1, 80 ms). Ours: `client/src/fx.cpp` `firework()`, placed the same way (Land.Radius = half the map width, as the orbit camera).
- Classes (rtti, td / vtable): ParticleHandlerService td 009202f8 vt 008612dc (Service; functions 005bfde0 [kill-all-emitters msg], 005c0080, 005c02c0, 005c09d0, 005c0d30, 005c0fd0 + more; creation 004eba10 "Create CLSID_ParticleHandlerService"; IsParticleEffectLogical callers 0057fcc0, 005a1fd0; kInvalidEmitterHandle). ParticleEmitterBase vt 00860870; ParticleEmitterEffectEntity vt 00860a6c/00860a84 (fns 005ba740..005bb350); ParticleEmitterGraphicEntity vt 00860db0 (005bd310, 005bd960); ParticleEmitterLogicEntity vt 00860fc4/00860fdc (005be0a0..005beb70); SnowParticleEmitterEntity vt 00820534; CParticle vt 00860734 (Particle.cpp 005b6f00..005b73d0); CParticleLandCollider vt 00861424, CParticleAttachedLandCollider vt 008607bc (005c1d50, 005c1e20, 005c2040); ParticleClass<T> template (ParticleClassInterface vt 00860810); ParticleColliderEmitterContainer vt 00877158; ParticleEmitterContainer vt 0087502c; EffectDetailsContainer vt 008772e4; ParticleMeshNamesContainer vt 00877300; XParticleSet vt 0088f058.

### 4. Water / sky / shadow / landscape classes (rtti td, vtable; fns = refs to source-file string)
| Class | vtable | Notes |
|---|---|---|
| WaterCgGraphicEntity | 00820990 | WaterCgGraphicEntity.cpp; fns 0048b460, 0048bc00, 0048c9f0; loads WaterVertexMain/Lighting/FragmentMain |
| WaterPlaneTweaks | 00877dd4 | XContainer: Glint/Shadow/Blend/Detail/ExtraGlint x {Centre,Inner,Middle,Outer,Rim}Color (rgba8) + GlintResource, ShadowResource, SkyBlendResource, DetailResource (str) |
| XWaterShader | 0089214c | XShader subclass |
| EFMV_RaiseWaterEventContainer | 00881f94 | cutscene water rise |
| SkyBoxEntity | 008203a4 | SkyBoxEntity.cpp fns 004857a0, 00485bb0, 00485de0; bins Skybox1-3 |
| XCloudShader | 0089216c | XShader subclass |
| LensFlareGraphicEntity / LensFlareContainer / LensFlareElementContainer | 0081cf34 / 00877ccc / 008745c4 | LensElementType kLens_SunGlow, Circle, FadedRing, Ring, FadedHex, Hex, RainbowRing |
| RainGraphicEntity / RainAudioEntity | 0081d6b4 / 0081d59c | weather |
| XFog, XLight, XLightGrid, XLightScope, XLightingEnable, XOglSpotLight | 008923f0, 0088eb44, 0088f860, 0088f6a8, 008923d0, 0088ebb4 | scene-graph lighting |
| LandscapeGraphicEntity / LandscapeLogicEntity | 0081c654 / 0081c810 | fns 0046fbb0, 00470b80 |
| LandChunk / LandFrame / LandFrameStore / LandTemplate | 00873424 / 008735c8 / 00873b24 / 00873c00 | landscape scene nodes |
| GenerateLandGeometry | 0081b174 | GLG_*.cpp: GLG_PC (004513e0, 00451ba0, 004520c0, 00454260), GLG_Shadow (00454e30, "VerticesToShadow < 0xfff0", shadow index cache), GLG_FringeBuilder |
| (heightmap shadowing) | - | "Shadowing heightmap..." in 00478780, 00482bd0 |
| XOglShaderManager, XOglContext, XOglRenderSurface, XOglTextureMap, XRenderManagerImpl<OpenGLImpl> | 008b3f10, 008b3654, 008b3fc4, 00892058, 00897a1c | GL backend |
| XBloomShape, XFocusBlurShape, XBlurEffect | -, -, 0088eb0c | exist in XOM scene graph; no matching CG shader (assumed unused on PC) |

## Text3DEntity backing (Name Backing.tga)

- [disasm 0x5fa860, 0x5fb4f0, 0x5fb6b0] Two 3-sprite sets are built per Text3D: `Text.Backing` and `Text.BackingBlimp`, both the same `Name Backing.tga` (Bundl09 descriptors differ only in a flag word, 0xf vs 0xd). Their colour is set once to (1, 1, 1) (0x5fa9ab, vtable +0x58) and no code path retints them: no team colour, no tweak colour, no per-frame alpha. `Text3DEntity::Render` (0x5fb4f0) only places them and sets visibility. The text colour (0x5fb640, 0x5fa680) is separate: the backing is never team coloured.
- [disasm 0x5fb6b0, 0x5fb4f0] `Text.Backing` is shown unless `0x51d8e0` (game state == 3) holds, then `Text.BackingBlimp` replaces it. Same texture, so the look is identical; what state 3 is was not traced [assumed irrelevant].
- [data] The exported `hud/name_backing.png` is correct: 128 x 128 RGBA, alpha is real (border opaque black, interior teal about (0, 107, 144) at alpha ~140, outer rows transparent). The translucent dark-edged teal plate is therefore the W4M look, drawn with the plain alpha blend [ours: blend mode not in the sprite set data we decode].
- [disasm 0x57b1e0] The fuse countdown is a Text3DEntity (`esi+0x6c`) like the others, so it has the backing; its world position is the payload + (0, FuseTimerGraphicOffset + Radius, 0) through 0x47a120, and the Text3D is centred on that point.
- [ours] `text3d()` (ui.cpp) now takes the centre of the plate and text as (x, y). Before, y was the top of our line box, which put the fuse number, crate-spy text and fuel half a line too low. Worm labels keep their previous placement (offsets in `Hud::draw` are ours; WormHealthNameEntity 0x5fdb70 uses font `HUD.FontAnim`, not `FE.Font`: not matched here).
