#include "game.h"
#include "hooks.h"
#include "texture.h"
#include "sdk.h"
#include "sdk_server.h"
#include "vr.h"
#include "offsets.h"
#include <iostream>
#include <cstddef>

namespace
{
	bool IsValidVRPlayerIndex(const Game* game, int index)
	{
		return game && index >= 0 &&
			static_cast<std::size_t>(index) < game->m_PlayersVRInfo.size();
	}
}

Hooks::Hooks(Game *game)
	: Hooks(game, false)
{
}

Hooks::Hooks(Game *game, bool renderOnly)
{
	if (MH_Initialize() != MH_OK)
	{
		Game::errorMsg("Failed to initialize MinHook. No hooks were installed.");
		return;
	}

	m_Game = game;
	m_VR = m_Game->m_VR;
	m_PushHUDStep = -999;
	m_PushedHud = true;

	if (renderOnly)
	{
		// Only the primary RenderView detour is enabled in the first stereo-render
		// pass. All other Source function hooks remain disabled.
		if (hkRenderView.createHook(
			(LPVOID)m_Game->m_Offsets->RenderView.address, &dRenderView) != 0)
		{
			MH_RemoveHook(MH_ALL_HOOKS);
			MH_Uninitialize();
			return;
		}

		if (hkRenderView.enableHook() != 0)
		{
			MH_DisableHook(MH_ALL_HOOKS);
			MH_RemoveHook(MH_ALL_HOOKS);
			MH_Uninitialize();
			return;
		}

		m_Ready = true;
		return;
	}

	if (!initSourceHooks())
	{
		Game::errorMsg("One or more 2009 hooks could not be created. All pending hooks were removed.");
		MH_RemoveHook(MH_ALL_HOOKS);
		MH_Uninitialize();
		return;
	}

#define ENABLE_HOOK(member) \
	do { \
		if ((member).enableHook() != 0) { \
			MH_DisableHook(MH_ALL_HOOKS); \
			MH_RemoveHook(MH_ALL_HOOKS); \
			MH_Uninitialize(); \
			return; \
		} \
	} while (0)

	// Keep this list in sync with the active createHook calls in initSourceHooks().
	// Missing an enable here silently leaves a feature's detour inactive.
	ENABLE_HOOK(hkRenderView);
	ENABLE_HOOK(hkCalcViewModelView);
	ENABLE_HOOK(hkProcessUsercmds);
	ENABLE_HOOK(hkReadUsercmd);
	ENABLE_HOOK(hkWriteUsercmd);
	ENABLE_HOOK(hkEyePosition);
	ENABLE_HOOK(hkPushRenderTargetAndViewport);
	ENABLE_HOOK(hkPopRenderTargetAndViewport);
	ENABLE_HOOK(hkVgui_Paint);
	ENABLE_HOOK(hkPrePushRenderTarget);
	ENABLE_HOOK(hkWeapon_ShootPosition);
	ENABLE_HOOK(hkTraceFirePortal);
	ENABLE_HOOK(hkCWeaponPortalgun_FirePortal);
	ENABLE_HOOK(hkDrawSelf);
	ENABLE_HOOK(hkClipTransform);
	ENABLE_HOOK(hkPlayerPortalled);
	ENABLE_HOOK(hkCreateMove);
	ENABLE_HOOK(hkComputeError);
	ENABLE_HOOK(hkUpdateObject);
	ENABLE_HOOK(hkUpdateObjectVM);
	ENABLE_HOOK(hkRotateObject);
	ENABLE_HOOK(hkEyeAngles);
	ENABLE_HOOK(hkGetDefaultFOV);
	ENABLE_HOOK(hkGetFOV);
	ENABLE_HOOK(hkGetViewModelFOV);
	ENABLE_HOOK(hkPrecache);
	ENABLE_HOOK(hkSetDrawOnlyForSplitScreenUser);
	ENABLE_HOOK(hkCHudCrosshair_ShouldDraw);

#undef ENABLE_HOOK
	m_Ready = true;
}

Hooks::~Hooks()
{
	if (m_Ready)
	{
		MH_DisableHook(MH_ALL_HOOKS);
		MH_RemoveHook(MH_ALL_HOOKS);
		MH_Uninitialize();
		m_Ready = false;
	}
}

bool Hooks::initSourceHooks()
{
	/*LPVOID pGetRenderTargetVFunc = (LPVOID)(m_Game->m_Offsets->GetRenderTarget.address);
	hkGetRenderTarget.createHook(pGetRenderTargetVFunc, &dGetRenderTarget);*/

	LPVOID pRenderViewVFunc = (LPVOID)(m_Game->m_Offsets->RenderView.address);
	if (hkRenderView.createHook(pRenderViewVFunc, &dRenderView) != 0) return false;

	LPVOID calcViewModelViewAddr = (LPVOID)(m_Game->m_Offsets->CalcViewModelView.address);
	if (hkCalcViewModelView.createHook(calcViewModelViewAddr, &dCalcViewModelView) != 0) return false;

	LPVOID ProcessUsercmdsAddr = (LPVOID)(m_Game->m_Offsets->ProcessUsercmds.address);
	if (hkProcessUsercmds.createHook(ProcessUsercmdsAddr, &dProcessUsercmds) != 0) return false;

	LPVOID ReadUserCmdAddr = (LPVOID)(m_Game->m_Offsets->ReadUserCmd.address);
	if (hkReadUsercmd.createHook(ReadUserCmdAddr, &dReadUsercmd) != 0) return false;

	/*LPVOID WriteUsercmdDeltaToBufferAddr = (LPVOID)(m_Game->m_Offsets->WriteUsercmdDeltaToBuffer.address);
	hkWriteUsercmdDeltaToBuffer.createHook(WriteUsercmdDeltaToBufferAddr, &dWriteUsercmdDeltaToBuffer);*/

	LPVOID WriteUsercmdAddr = (LPVOID)(m_Game->m_Offsets->WriteUsercmd.address);
	if (hkWriteUsercmd.createHook(WriteUsercmdAddr, &dWriteUsercmd) != 0) return false;

	/*LPVOID AdjustEngineViewportAddr = (LPVOID)(m_Game->m_Offsets->AdjustEngineViewport.address);
	if (hkAdjustEngineViewport.createHook(AdjustEngineViewportAddr, &dAdjustEngineViewport) != 0) return false;

	LPVOID ViewportAddr = (LPVOID)(m_Game->m_Offsets->Viewport.address);
	if (hkViewport.createHook(ViewportAddr, &dViewport) != 0) return false;

	LPVOID GetViewportAddr = (LPVOID)(m_Game->m_Offsets->GetViewport.address);
	hkGetViewport.createHook(GetViewportAddr, &dGetViewport);*/

	LPVOID EyePositionAddr = (LPVOID)(m_Game->m_Offsets->EyePosition.address);
	if (hkEyePosition.createHook(EyePositionAddr, &dEyePosition) != 0) return false;

	/*LPVOID DrawModelExecuteAddr = (LPVOID)(m_Game->m_Offsets->DrawModelExecute.address);
	hkDrawModelExecute.createHook(DrawModelExecuteAddr, &dDrawModelExecute);*/

	LPVOID PushRenderTargetAddr = (LPVOID)(m_Game->m_Offsets->PushRenderTargetAndViewport.address);
	if (hkPushRenderTargetAndViewport.createHook(PushRenderTargetAddr, &dPushRenderTargetAndViewport) != 0) return false;

	LPVOID PopRenderTargetAddr = (LPVOID)(m_Game->m_Offsets->PopRenderTargetAndViewport.address);
	if (hkPopRenderTargetAndViewport.createHook(PopRenderTargetAddr, &dPopRenderTargetAndViewport) != 0) return false;

	LPVOID VGui_PaintAddr = (LPVOID)(m_Game->m_Offsets->VGui_Paint.address);
	if (hkVgui_Paint.createHook(VGui_PaintAddr, &dVGui_Paint) != 0) return false;

	/*LPVOID IsSplitScreenAddr = (LPVOID)(m_Game->m_Offsets->IsSplitScreen.address);
	hkIsSplitScreen.createHook(IsSplitScreenAddr, &dIsSplitScreen);*/

	LPVOID PrePushRenderTargetAddr = (LPVOID)(m_Game->m_Offsets->PrePushRenderTarget.address);
	if (hkPrePushRenderTarget.createHook(PrePushRenderTargetAddr, &dPrePushRenderTarget) != 0) return false;

	/*LPVOID GetFullScreenTextureAddr = (LPVOID)(m_Game->m_Offsets->GetFullScreenTexture.address);
	hkGetFullScreenTexture.createHook(GetFullScreenTextureAddr, &dGetFullScreenTexture);*/

	LPVOID Weapon_ShootPositionAddr = (LPVOID)(m_Game->m_Offsets->Weapon_ShootPosition.address);
	if (hkWeapon_ShootPosition.createHook(Weapon_ShootPositionAddr, &dWeapon_ShootPosition) != 0) return false;
	
	LPVOID TraceFirePortalAddr = (LPVOID)(m_Game->m_Offsets->TraceFirePortalServer.address);
	if (hkTraceFirePortal.createHook(TraceFirePortalAddr, &dTraceFirePortal) != 0) return false;

	if (hkCWeaponPortalgun_FirePortal.createHook((LPVOID)m_Game->m_Offsets->CWeaponPortalgun_FirePortal.address, &dCWeaponPortalgun_FirePortal) != 0) return false;

	LPVOID DrawSelfAddr = (LPVOID)(m_Game->m_Offsets->DrawSelf.address);
	if (hkDrawSelf.createHook(DrawSelfAddr, &dDrawSelf) != 0) return false;
	
	LPVOID ClipTransformAddr = (LPVOID)(m_Game->m_Offsets->ClipTransform.address);
	if (hkClipTransform.createHook(ClipTransformAddr, &dClipTransform) != 0) return false;

	// Portalling
	LPVOID PlayerPortalledAddr = (LPVOID)(m_Game->m_Offsets->PlayerPortalled.address);
	if (hkPlayerPortalled.createHook(PlayerPortalledAddr, &dPlayerPortalled) != 0) return false;

	UTIL_Portal_FirstAlongRay = (tUTIL_Portal_FirstAlongRay)m_Game->m_Offsets->UTIL_Portal_FirstAlongRay.address;
	UTIL_IntersectRayWithPortal = (tUTIL_IntersectRayWithPortal)m_Game->m_Offsets->UTIL_IntersectRayWithPortal.address;

	LPVOID CreateMoveAddr = (LPVOID)(m_Game->m_Offsets->CreateMove.address);
	if (hkCreateMove.createHook(CreateMoveAddr, &dCreateMove) != 0) return false;

	// Grababbles
	if (hkComputeError.createHook((LPVOID)(m_Game->m_Offsets->ComputeError.address), &dComputeError) != 0) return false;
	if (hkUpdateObject.createHook((LPVOID)(m_Game->m_Offsets->UpdateObject.address), &dUpdateObject) != 0) return false;
	if (hkUpdateObjectVM.createHook((LPVOID)(m_Game->m_Offsets->UpdateObjectVM.address), &dUpdateObjectVM) != 0) return false;
	if (hkRotateObject.createHook((LPVOID)(m_Game->m_Offsets->RotateObject.address), &dRotateObject) != 0) return false;
	if (hkEyeAngles.createHook((LPVOID)(m_Game->m_Offsets->EyeAngles.address), &dEyeAngles) != 0) return false;

	// Portal Gun VFX
	if (hkGetDefaultFOV.createHook((LPVOID)(m_Game->m_Offsets->GetDefaultFOV.address), &dGetDefaultFOV) != 0) return false;
	if (hkGetFOV.createHook((LPVOID)(m_Game->m_Offsets->GetFOV.address), &dGetFOV) != 0) return false;
	if (hkGetViewModelFOV.createHook((LPVOID)(m_Game->m_Offsets->GetViewModelFOV.address), &dGetViewModelFOV) != 0) return false;
	
	// Laser Pointer
	GetPortalPlayer = (tGetPortalPlayer)m_Game->m_Offsets->GetPortalPlayer.address;
	CreatePingPointer = (tCreatePingPointer)m_Game->m_Offsets->CreatePingPointer.address;
	PrecacheParticleSystem = (tPrecacheParticleSystem)m_Game->m_Offsets->PrecacheParticleSystem.address;
	if (hkPrecache.createHook((LPVOID)(m_Game->m_Offsets->Precache.address), &dPrecache) != 0) return false;
	if (hkSetDrawOnlyForSplitScreenUser.createHook((LPVOID)m_Game->m_Offsets->SetDrawOnlyForSplitScreenUser.address, &dSetDrawOnlyForSplitScreenUser) != 0) return false;
	if (hkCHudCrosshair_ShouldDraw.createHook((LPVOID)m_Game->m_Offsets->CHudCrosshair_ShouldDraw.address, &dCHudCrosshair_ShouldDraw) != 0) return false;

	//
	EntityIndex = (tEntindex)m_Game->m_Offsets->CBaseEntity_entindex.address;
	GetOwner = (tGetOwner)m_Game->m_Offsets->GetOwner.address;
	return true;
} 

bool __fastcall Hooks::dCHudCrosshair_ShouldDraw(void* ecx, void* edx) {
	bool shouldDraw = hkCHudCrosshair_ShouldDraw.fOriginal(ecx);

	m_VR->m_DrawCrosshair = shouldDraw;

	return ((m_VR->m_AimMode == 1) ? shouldDraw : false);
}

void __fastcall Hooks::dPrecache(void* ecx, void* edx) {
	hkPrecache.fOriginal(ecx);
	PrecacheParticleSystem("robot_point_beam");
}

void __fastcall Hooks::dClientThink(void* ecx, void* edx) {
	hkClientThink.fOriginal(ecx);
}

void __fastcall Hooks::dSetDrawOnlyForSplitScreenUser(void* ecx, void* edx, int nSlot) {
	hkSetDrawOnlyForSplitScreenUser.fOriginal(ecx, -1);
}

ITexture *__fastcall Hooks::dGetFullScreenTexture()
{
	ITexture *result = hkGetFullScreenTexture.fOriginal();
	return result;
}

ITexture* __fastcall Hooks::dGetRenderTarget(void* ecx, void* edx)
{
	ITexture* result = hkGetRenderTarget.fOriginal(ecx);
	return result;
}

void __fastcall Hooks::dRenderView(void *ecx, void *edx, CViewSetup &setup, CViewSetup &hudViewSetup, int nClearFlags, int whatToDraw)
{
	// The 2009 port starts with one independently verified render hook. Keep the
	// menu on its normal backbuffer overlay; only split the actual in-game view.
	if (!m_Game || !m_Game->m_Initialized || !m_VR || !m_VR->m_IsInitialized ||
		!m_Game->m_EngineClient || !m_Game->m_MaterialSystem ||
		!m_Game->m_EngineClient->IsInGame())
	{
		return hkRenderView.fOriginal(ecx, setup, hudViewSetup, nClearFlags, whatToDraw);
	}

	if (m_Game->m_VguiSurface && m_Game->m_VguiSurface->IsCursorVisible())
		return hkRenderView.fOriginal(ecx, setup, hudViewSetup, nClearFlags, whatToDraw);

	if (!m_VR->m_CreatedVRTextures)
		m_VR->CreateVRTextures();

	if (!m_VR->m_CreatedVRTextures || !m_VR->m_LeftEyeTexture || !m_VR->m_RightEyeTexture)
		return hkRenderView.fOriginal(ecx, setup, hudViewSetup, nClearFlags, whatToDraw);

	IMatRenderContext* renderContext = m_Game->m_MaterialSystem->GetRenderContext();
	if (!renderContext)
		return hkRenderView.fOriginal(ecx, setup, hudViewSetup, nClearFlags, whatToDraw);

	const Vector baseOrigin = setup.origin;
	const Vector hmdAngle = m_VR->GetViewAngle();

	setup.x = 0;
	setup.y = 0;
	setup.width = m_VR->m_RenderWidth;
	setup.height = m_VR->m_RenderHeight;
	setup.m_nUnscaledWidth = m_VR->m_RenderWidth;
	setup.m_nUnscaledHeight = m_VR->m_RenderHeight;
	setup.fov = m_VR->m_Fov;
	setup.fovViewmodel = m_VR->m_Fov;
	setup.m_flAspectRatio = m_VR->m_Aspect;
	setup.zNear = 6;
	setup.zNearViewmodel = 2;
	setup.angles = hmdAngle;

	hudViewSetup.width = m_VR->m_RenderWidth;
	hudViewSetup.height = m_VR->m_RenderHeight;
	hudViewSetup.m_nUnscaledWidth = m_VR->m_RenderWidth;
	hudViewSetup.m_nUnscaledHeight = m_VR->m_RenderHeight;
	hudViewSetup.fov = m_VR->m_Fov;
	hudViewSetup.m_flAspectRatio = m_VR->m_Aspect;

	CViewSetup leftEyeView = setup;
	CViewSetup rightEyeView = setup;

	// This first pass applies the headset's measured IPD and orientation directly.
	// Portal-plane eye clipping is intentionally deferred until those server hooks
	// have their July 2009 calling conventions verified.
	leftEyeView.origin = m_VR->GetViewOriginLeft(baseOrigin);
	leftEyeView.angles = hmdAngle;
	renderContext->SetRenderTarget(m_VR->m_LeftEyeTexture);
	renderContext->Release();
	hkRenderView.fOriginal(ecx, leftEyeView, hudViewSetup, nClearFlags, whatToDraw);

	renderContext = m_Game->m_MaterialSystem->GetRenderContext();
	if (!renderContext)
		return;
	renderContext->SetRenderTarget(m_VR->m_RightEyeTexture);
	renderContext->Release();

	rightEyeView.origin = m_VR->GetViewOriginRight(baseOrigin);
	rightEyeView.angles = hmdAngle;
	hkRenderView.fOriginal(ecx, rightEyeView, hudViewSetup, nClearFlags, whatToDraw);

	renderContext = m_Game->m_MaterialSystem->GetRenderContext();
	if (renderContext)
	{
		renderContext->SetRenderTarget(NULL);
		renderContext->Release();
	}

	// DXVK's Present path resolves these D3D9 eye surfaces to Vulkan images and
	// submits the pair to IVRCompositor after this frame has rendered.
	m_VR->m_RenderedNewFrame = true;
}

bool __fastcall Hooks::dCreateMove(void *ecx, void *edx, float flInputSampleTime, CUserCmd *cmd)
{
	if (!cmd->command_number)
		return hkCreateMove.fOriginal(ecx, flInputSampleTime, cmd);

	if (m_VR->m_IsVREnabled)
	{
		cmd->viewangles = m_VR->m_HmdAngAbs;

		vr::InputAnalogActionData_t analogActionData;
		if (m_VR->GetAnalogActionData(m_VR->m_ActionWalk, analogActionData)) {
			// Run toward other guy
			cmd->buttons &= ~(IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT);

			cmd->forwardmove += analogActionData.y * MAX_LINEAR_SPEED;
			cmd->sidemove += analogActionData.x * MAX_LINEAR_SPEED;

			// We'll only be moving fwd or sideways
			cmd->upmove = 0.0f;

			if (cmd->forwardmove > 0.0f)
			{
				cmd->buttons |= IN_FORWARD;
			}
			else if (cmd->forwardmove < 0.0f)
			{
				cmd->buttons |= IN_BACK;
			}

			if (cmd->sidemove > 0.0f)
			{
				cmd->buttons |= IN_MOVELEFT;
			}
			else if (cmd->sidemove < 0.0f)
			{
				cmd->buttons |= IN_MOVERIGHT;
			}

		}

		if (m_VR->m_RoomscaleActive)
		{
			// How much have we moved since last CreateMove?
			Vector setupOriginToHMD = (m_VR->m_HmdPosRelativeRaw - m_VR->m_HmdPosRelativeRawPrev) * m_VR->m_VRScale; //m_VR->m_HmdPosRelative - m_VR->m_HmdPosRelativePrev;
			m_VR->m_HmdPosRelativeRawPrev = m_VR->m_HmdPosRelativeRaw;

			setupOriginToHMD.z = 0;
			float distance = VectorLength(setupOriginToHMD);
			if (distance > 0)
			{
				float forwardSpeed = DotProduct2D(setupOriginToHMD, m_VR->m_HmdForward);
				float sideSpeed = DotProduct2D(setupOriginToHMD, m_VR->m_HmdRight);
				cmd->forwardmove += distance * forwardSpeed;
				cmd->sidemove += distance * sideSpeed;

				// Let's update the position and the previous too
				/*m_VR->m_HmdPosRelative -= setupOriginToHMD;
				m_VR->m_HmdPosRelativePrev = m_VR->m_HmdPosRelative;*/

				/*m_VR->m_Center += m_VR->m_HmdPosRelativeRaw - m_VR->m_HmdPosRelativeRawPrev;
				m_VR->m_HmdPosRelativeRawPrev = m_VR->m_HmdPosRelativeRaw;*/

				//m_VR->ResetPosition();
			}
		}
	}

	return false;
}

void __fastcall Hooks::dEndFrame(void *ecx, void *edx)
{
	return hkEndFrame.fOriginal(ecx);
}

void __fastcall Hooks::dCalcViewModelView(void *ecx, void *edx, const Vector &eyePosition, const QAngle &eyeAngles)
{
	Vector vecNewOrigin = eyePosition;
	QAngle vecNewAngles = eyeAngles;

	//std::cout << "dCalcViewModelView: (" << m_VR->m_IsVREnabled << ")\n";

	if (m_VR->m_IsVREnabled)
	{
		vecNewOrigin = m_VR->GetRecommendedViewmodelAbsPos(eyePosition);
		vecNewAngles = m_VR->GetRecommendedViewmodelAbsAngle();
	}


	return hkCalcViewModelView.fOriginal(ecx, vecNewOrigin, vecNewAngles);
}

float __fastcall Hooks::dProcessUsercmds(void *ecx, void *edx, edict_t *player, void *buf, int numcmds, int totalcmds, int dropped_packets, bool ignore, bool paused)
{
	int index = -1;
	if (player && player->m_pUnk)	{
		Server_BaseEntity* pPlayer = (Server_BaseEntity*)player->m_pUnk->GetBaseEntity();
		if (pPlayer)
			index = EntityIndex(pPlayer);
	}

	const int previousUsercmdID = m_Game->m_CurrentUsercmdID;
	m_Game->m_CurrentUsercmdID = IsValidVRPlayerIndex(m_Game, index) ? index : -1;
	const float result = hkProcessUsercmds.fOriginal(ecx, player, buf, numcmds, totalcmds, dropped_packets, ignore, paused);
	m_Game->m_CurrentUsercmdID = previousUsercmdID;
	return result;
}

int Hooks::dWriteUsercmd(bf_write *buf, CUserCmd *to, CUserCmd *from)
{
	auto result =  hkWriteUsercmd.fOriginal(buf, to, from);

	// Let's write our stuff into the buffer
	if (m_VR->m_IsVREnabled)
	{
		Vector controllerPos = m_VR->GetRightControllerAbsPos();
		QAngle controllerAngles = m_VR->GetRightControllerAbsAngle();

		buf->WriteChar(-2);
		buf->WriteBitVec3Coord(controllerPos);
		buf->WriteBitAngles(controllerAngles);
	}

	return result;
}

int Hooks::dReadUsercmd(bf_read *buf, CUserCmd* move, CUserCmd* from)
{
	auto result = hkReadUsercmd.fOriginal(buf, move, from);

	const int playerIndex = m_Game->m_CurrentUsercmdID;
	const bool hasPlayerSlot = IsValidVRPlayerIndex(m_Game, playerIndex);
	const auto bitPosition = buf->Tell();
	const int marker = buf->ReadChar();

	// The custom controller payload follows every serialized CUserCmd. Always
	// consume it when its marker is present, even if the server entity index is
	// outside our player table; otherwise the following usercmd stream is misaligned.
	if (marker == -2)
	{
		Vector controllerPosition;
		QAngle controllerAngle;
		buf->ReadBitVec3Coord(controllerPosition);
		buf->ReadBitAngles(controllerAngle);

		if (hasPlayerSlot)
		{
			auto& vrPlayer = m_Game->m_PlayersVRInfo[static_cast<std::size_t>(playerIndex)];
			vrPlayer.isUsingVR = true;
			vrPlayer.controllerPos = controllerPosition;
			vrPlayer.controllerAngle = controllerAngle;
		}
	}
	else
	{
		if (hasPlayerSlot)
			m_Game->m_PlayersVRInfo[static_cast<std::size_t>(playerIndex)].isUsingVR = false;
		buf->Seek(bitPosition);
	}

	return result;
}


void Hooks::dAdjustEngineViewport(int &x, int &y, int &width, int &height)
{
	width = m_VR->m_RenderWidth;
	height = m_VR->m_RenderHeight;

	hkAdjustEngineViewport.fOriginal(x, y, width, height);
}

void Hooks::dGetViewport(void *ecx, void *edx, int &x, int &y, int &width, int &height)
{
	hkGetViewport.fOriginal(ecx, x, y, width, height);

	width = m_VR->m_RenderWidth;
	height = m_VR->m_RenderHeight;
}

int Hooks::dGetPrimaryAttackActivity(void *ecx, void *edx, void *meleeInfo)
{
	return hkGetPrimaryAttackActivity.fOriginal(ecx, meleeInfo);
}

Vector *Hooks::dEyePosition(void *ecx, void *edx, Vector *eyePos)
{
	Vector *result = hkEyePosition.fOriginal(ecx, eyePos);
	return result;
}

// We'll keep this for... future reference!
void Hooks::dDrawModelExecute(void *ecx, void *edx, void *state, const ModelRenderInfo_t &info, void *pCustomBoneToWorld)
{
	if (info.pModel)
	{
		std::string modelName = m_Game->m_ModelInfo->GetModelName(info.pModel);
		if (modelName.find("/arms/") != std::string::npos)
		{
			m_Game->m_ArmsMaterial = m_Game->m_MaterialSystem->FindMaterial(modelName.c_str(), "Model textures");
			m_Game->m_ArmsModel = info.pModel;
			m_Game->m_CachedArmsModel = true;
		}
	}

	if (info.pModel && info.pModel == m_Game->m_ArmsModel)
	{
		m_Game->m_ArmsMaterial->SetMaterialVarFlag(MATERIAL_VAR_NO_DRAW, true);
		m_Game->m_ModelRender->ForcedMaterialOverride(m_Game->m_ArmsMaterial);
		hkDrawModelExecute.fOriginal(ecx, state, info, pCustomBoneToWorld);
		m_Game->m_ModelRender->ForcedMaterialOverride(NULL);
		return;
	}

	hkDrawModelExecute.fOriginal(ecx, state, info, pCustomBoneToWorld);
}

void Hooks::dPushRenderTargetAndViewport(void *ecx, void *edx, ITexture *pTexture, ITexture *pDepthTexture, int nViewX, int nViewY, int nViewW, int nViewH)
{
	if (m_VR->m_CreatedVRTextures && !m_PushedHud)
	{
		pTexture = m_VR->m_HUDTexture;

		//pTexture = m_VR->m_RightEyeTexture;

		IMatRenderContext *renderContext = m_Game->m_MaterialSystem->GetRenderContext();
		renderContext->ClearBuffers(false, true, true);
		renderContext->Release();

		hkPushRenderTargetAndViewport.fOriginal(ecx, pTexture, pDepthTexture, nViewX, nViewY, nViewW, nViewH);

		renderContext = m_Game->m_MaterialSystem->GetRenderContext();
		renderContext->OverrideAlphaWriteEnable(true, true);
		renderContext->ClearColor4ub(0, 0, 0, 0);
		renderContext->ClearBuffers(true, false);
		renderContext->Release();

		m_VR->m_RenderedHud = true;
		m_PushedHud = true;
	}
	else
	{
		hkPushRenderTargetAndViewport.fOriginal(ecx, pTexture, pDepthTexture, nViewX, nViewY, nViewW, nViewH);
	}
}

void Hooks::dPopRenderTargetAndViewport(void *ecx, void *edx)
{
	if (!m_VR->m_CreatedVRTextures)
		return hkPopRenderTargetAndViewport.fOriginal(ecx);

	//std::cout << "dPopRenderTargetAndViewport: " << m_PushHUDStep << "\n";

	m_PushHUDStep = 0;

	if (m_PushedHud)
	{
		IMatRenderContext* renderContext = m_Game->m_MaterialSystem->GetRenderContext();
		renderContext->OverrideAlphaWriteEnable(false, true);
		renderContext->ClearColor4ub(0, 0, 0, 255);
		renderContext->Release();
	}

	hkPopRenderTargetAndViewport.fOriginal(ecx);
}

void Hooks::dVGui_Paint(void *ecx, void *edx, int mode)
{
	if (!m_VR->m_CreatedVRTextures || m_VR->m_Game->m_VguiSurface->IsCursorVisible())
		return hkVgui_Paint.fOriginal(ecx, mode);

	//std::cout << "dVGui_Paint\n";

	if (m_PushedHud)
		mode = PAINT_UIPANELS | PAINT_INGAMEPANELS;

	hkVgui_Paint.fOriginal(ecx, mode);
}

int Hooks::dIsSplitScreen()
{
	//std::cout << "dIsSplitScreen: " << m_PushHUDStep << "\n";

	if (m_PushHUDStep == 0)
		++m_PushHUDStep;
	else
		m_PushHUDStep = -999;

	return hkIsSplitScreen.fOriginal();
}

DWORD *Hooks::dPrePushRenderTarget(void *ecx, void *edx, int a2)
{
	//std::cout << "dPrePushRenderTarget: " << m_PushHUDStep << "\n";

	if (m_PushHUDStep == 1)
		++m_PushHUDStep;
	else
		m_PushHUDStep = -999;

	return hkPrePushRenderTarget.fOriginal(ecx, a2);
}

Vector* Hooks::dWeapon_ShootPosition(void* ecx, void* edx, Vector* eyePos)
{
	Vector* result = hkWeapon_ShootPosition.fOriginal(ecx, eyePos);
	if (!result)
		return nullptr;

	int localIndex = m_Game->m_EngineClient->GetLocalPlayer();
	int index = EntityIndex(ecx);

	if (IsValidVRPlayerIndex(m_Game, index))
	{
		const auto& vrPlayer = m_Game->m_PlayersVRInfo[static_cast<std::size_t>(index)];
		if (m_VR->m_IsVREnabled && localIndex == index)
			*result = m_VR->GetRightControllerAbsPos();
		else if (vrPlayer.isUsingVR)
			*result = vrPlayer.controllerPos;
	}

	return result;
}

void* Hooks::dCWeaponPortalgun_FirePortal(void* ecx, void* edx, bool bPortal2, Vector* pVector) {
	bool wasTrue = m_VR->m_OverrideEyeAngles;

	m_VR->m_OverrideEyeAngles = true;

	auto result = hkCWeaponPortalgun_FirePortal.fOriginal(ecx, bPortal2, pVector);

	if (!wasTrue)
		m_VR->m_OverrideEyeAngles = false;

	return result;
}

bool __fastcall Hooks::dTraceFirePortal(void* ecx, void* edx, const Vector& vTraceStart, const Vector& vDirection, bool bPortal2, int iPlacedBy, void* tr) //trace_tx& tr, Vector& vFinalPosition //  , Vector& vFinalPosition, QAngle& qFinalAngles, int iPlacedBy, bool bTest /*= false*/
{
	Vector vNewTraceStart = vTraceStart;
	Vector vNewDirection = vDirection;

	if (iPlacedBy == 2) {
		int localIndex = m_Game->m_EngineClient->GetLocalPlayer();

		auto owner = GetOwner(ecx);

		if (owner) {
			int index = EntityIndex(owner);
			if (IsValidVRPlayerIndex(m_Game, index))
			{
				const auto& vrPlayer = m_Game->m_PlayersVRInfo[static_cast<std::size_t>(index)];
				if (m_VR->m_IsVREnabled && localIndex == index) {
					vNewTraceStart = m_VR->GetRightControllerAbsPos();
					vNewDirection = m_VR->m_RightControllerForward;
				}
				else if (vrPlayer.isUsingVR)
				{
					vNewTraceStart = vrPlayer.controllerPos;
					Vector fwd, rt, up;
					QAngle::AngleVectors(vrPlayer.controllerAngle, &fwd, &rt, &up);
					vNewDirection = fwd;
				}
			}
		}
	}

	return hkTraceFirePortal.fOriginal(ecx, vNewTraceStart, vNewDirection, bPortal2, iPlacedBy, tr);
}

void __fastcall Hooks::dPlayerPortalled(void* ecx, void* edx, void* a2, __int64 a3)
{
	CBaseEntity* pBaseEntity = (CBaseEntity*)ecx;

	QAngle angAbsRotationBefore;
	m_Game->m_EngineClient->GetViewAngles(angAbsRotationBefore);

	hkPlayerPortalled.fOriginal(ecx, a2, a3);

	QAngle angAbsRotationAfter;
	m_Game->m_EngineClient->GetViewAngles(angAbsRotationAfter);

	if (angAbsRotationBefore != angAbsRotationAfter) {
		m_VR->m_PortalRotationOffset = angAbsRotationAfter - angAbsRotationBefore;
		m_VR->m_ApplyPortalRotationOffset = true;
	}

	return;
}

int Hooks::dGetModeHeight(void* ecx, void* edx) {
	//std::cout << "dGetModeHeight\n";
	return m_VR->m_RenderHeight;
}

bool Hooks::dClipTransform(const Vector& point, Vector* pScreen)
{
	return hkClipTransform.fOriginal(point, pScreen);
}

bool Hooks::ScreenTransform(const Vector& point, Vector* pScreen, int width, int height)
{
	bool retval = hkClipTransform.fOriginal(point, pScreen);

	pScreen->x = 0.5f * (pScreen->x + 1.0f) * width;
	pScreen->y = 0.5f * (-pScreen->y + 1.0f) * height;

	return retval;
}

int __fastcall Hooks::dDrawSelf(void* ecx, void* edx, int x, int y, int w, int h, const void* clr, float flApparentZ) {
	//std::cout << "dDrawSelf - X: " << x << ", Y: " << y << ", W: " << w << ", H: " << h << ", Z: " << flApparentZ << "\n";

	//int playerIndex = m_Game->m_EngineClient->GetLocalPlayer();

	//auto viewport = m_Game->m_ClientMode->GetViewport();

	int newX = x;
	int	newY = y;

	if (m_VR->m_IsVREnabled)
	{
		int windowWidth, windowHeight;
		m_Game->m_MaterialSystem->GetRenderContext()->GetWindowSize(windowWidth, windowHeight);

		Vector screen = { 0, 0, 0 };

		//Vector vec = m_VR->m_AimPos - m_VR->GetRightControllerAbsPos();

		//newZ = 1.0 / sqrt(vec.x * vec.x + vec.y * vec.y + vec.z * vec.z);

		ScreenTransform(m_VR->m_AimPos, &screen, m_VR->m_RenderWidth, m_VR->m_RenderHeight);

		int offsetX = x - (windowWidth * 0.5f);
		int offsetY = y - (windowHeight * 0.5f);

		newX = screen.x + offsetX;
		newY = screen.y + offsetY;
	}

	return hkDrawSelf.fOriginal(ecx, newX, newY, w, h, clr, flApparentZ);
}

void __cdecl Hooks::dVGui_GetHudBounds(int slot, int& x, int& y, int& w, int& h) {
	if (m_VR->m_IsVREnabled && !m_Game->m_VguiSurface->IsCursorVisible())
	{
		x = y = 0;
		w = m_VR->m_RenderWidth;
		h = m_VR->m_RenderHeight;
	} else {
		hkVGui_GetHudBounds.fOriginal(slot, x, y, w, h);
	}

	//std::cout << "dVGui_GetHudBounds - X: " << x << ", Y: " << y << ", W: " << w << ", H: " << h << "\n";
}

void __cdecl Hooks::dVGui_GetPanelBounds(int slot, int& x, int& y, int& w, int& h) {
	if (m_VR->m_IsVREnabled && !m_Game->m_VguiSurface->IsCursorVisible())
	{
		x = y = 0;
		w = m_VR->m_RenderWidth;
		h = m_VR->m_RenderHeight;
	}
	else {
		hkVGui_GetPanelBounds.fOriginal(slot, x, y, w, h);
	}

	//std::cout << "dVGui_GetPanelBounds - X: " << x << ", Y: " << y << ", W: " << w << ", H: " << h << "\n";
}

void __cdecl Hooks::dVGUI_UpdateScreenSpaceBounds(int nNumSplits, int sx, int sy, int sw, int sh) {
	hkVGUI_UpdateScreenSpaceBounds.fOriginal(nNumSplits, sx, sy, m_VR->m_RenderWidth, m_VR->m_RenderHeight);
}

void __cdecl Hooks::dVGui_GetTrueScreenSize(int &w, int &h) {
	w = m_VR->m_RenderWidth;
	h = m_VR->m_RenderHeight;
}

void __fastcall Hooks::dGetScreenSize(void* ecx, void* edx, int& wide, int& tall) {
	//hkGetScreenSize.fOriginal(ecx, wide, tall);
	wide = m_VR->m_RenderWidth;
	tall = m_VR->m_RenderHeight;
}

void __cdecl Hooks::dGetHudSize(int& w, int& h) {
	w = m_VR->m_RenderWidth;
	h = m_VR->m_RenderHeight;
}

void __fastcall Hooks::dPush2DView(void* ecx, void* edx, IMatRenderContext* pRenderContext, const CViewSetup& view, int nFlags, ITexture* pRenderTarget, void* frustumPlanes) {
	m_PushedHud = false;

	return hkPush2DView.fOriginal(ecx, pRenderContext, view, nFlags, pRenderTarget, frustumPlanes);
}

void __fastcall Hooks::dRender(void* ecx, void* edx, vrect_t* rect) {
	//std::cout << "dRender - X: " << rect->x << ", Y: " << rect->y << ", W: " << rect->width << ", H: " << rect->height  << "\n";

	return hkRender.fOriginal(ecx, rect);
}

void __fastcall Hooks::dSetBounds(void* ecx, void* edx, int x, int y, int w, int h) {
	std::cout << "dSetBounds - X: " << x << ", Y: " << y << ", W: " << w << ", H: " << h << "\n";

	hkSetBounds.fOriginal(ecx, x, y, m_VR->m_RenderWidth, m_VR->m_RenderHeight);
}

void __fastcall Hooks::dSetSize(void* ecx, void* edx, int wide, int tall) {
	hkSetSize.fOriginal(ecx, wide, tall);

	//std::cout << "dSetSize - Wide: " << wide << ", Tall: " << tall  << "\n";
}

void __fastcall Hooks::dGetClipRect(void* ecx, void* edx, int& x0, int& y0, int& x1, int& y1) {
	hkGetClipRect.fOriginal(ecx, x0, y0, x1, y1);

	//std::cout << "dGetClipRect - X: " << x0 << ", Y: " << y0 << ", W: " << x1 << ", H: " << y1  << "\n";
}

double __fastcall Hooks::dComputeError(void* ecx, void* edx) {
	bool wasTrue = m_VR->m_OverrideEyeAngles;

	m_VR->m_OverrideEyeAngles = true;

	double computedError = hkComputeError.fOriginal(ecx);

	if (!wasTrue)
		m_VR->m_OverrideEyeAngles = false;

	return computedError;
}

bool __fastcall Hooks::dUpdateObject(void* ecx, void* edx, void* pPlayer, float flError, bool bIsTeleport) {
	bool wasTrue = m_VR->m_OverrideEyeAngles;

	m_VR->m_OverrideEyeAngles = true;

	bool value = hkUpdateObject.fOriginal(ecx, pPlayer, flError, bIsTeleport);

	if (!wasTrue)
		m_VR->m_OverrideEyeAngles = false;

	return value;
}

bool __fastcall Hooks::dUpdateObjectVM(void* ecx, void* edx, void* pPlayer, float flError) {
	bool wasTrue = m_VR->m_OverrideEyeAngles;

	m_VR->m_OverrideEyeAngles = true;

	bool value = hkUpdateObjectVM.fOriginal(ecx, pPlayer, flError);

	if (!wasTrue)
		m_VR->m_OverrideEyeAngles = false;

	return value;
}

// This function is apparently not used by Portal 2, remove?
void __fastcall Hooks::dRotateObject(void* ecx, void* edx, void* pPlayer, float fRotAboutUp, float fRotAboutRight, bool bUseWorldUpInsteadOfPlayerUp) {
	bool wasTrue = m_VR->m_OverrideEyeAngles;

	m_VR->m_OverrideEyeAngles = true;

	hkRotateObject.fOriginal(ecx, pPlayer, fRotAboutUp, fRotAboutRight, bUseWorldUpInsteadOfPlayerUp);

	if (!wasTrue)
		m_VR->m_OverrideEyeAngles = false;
}

// This is CPlayerBase, do we also need to hook CPortalPlayer? can the same function be used by both?
// This works for release, but why was it crashing before??? TODO: buy a c++ book...
QAngle& __fastcall Hooks::dEyeAngles(void* ecx, void* edx) {
	if (m_VR->m_OverrideEyeAngles) {
		int localIndex = m_Game->m_EngineClient->GetLocalPlayer();
		int index = EntityIndex(ecx);
		if (IsValidVRPlayerIndex(m_Game, index))
		{
			auto& vrPlayer = m_Game->m_PlayersVRInfo[static_cast<std::size_t>(index)];
			if (m_VR->m_IsVREnabled && localIndex == index)
				return m_VR->GetRightControllerAbsAngleConst();
			else if (vrPlayer.isUsingVR)
				return vrPlayer.controllerAngle;
		}
	}

	return hkEyeAngles.fOriginal(ecx);
}

int __fastcall Hooks::dGetDefaultFOV(void* ecx, void* edx) {
	return m_VR->m_Fov;
}

double __fastcall Hooks::dGetFOV(void* ecx, void* edx) {
	return m_VR->m_Fov;
}

float __fastcall Hooks::dGetViewModelFOV(void* ecx, void* edx) {
	return m_VR->m_Fov;
}
