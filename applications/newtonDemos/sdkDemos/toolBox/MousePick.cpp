/* Copyright (c) <2009> <Newton Game Dynamics>
* 
* This software is provided 'as-is', without any express or implied
* warranty. In no event will the authors be held liable for any damages
* arising from the use of this software.
* 
* Permission is granted to anyone to use this software for any purpose,
* including commercial applications, and to alter it and redistribute it
* freely
*/

#include <toolbox_stdafx.h>

#include "MousePick.h"
#include "JointLibrary.h"
//#include "CustomKinematicController.h"

static dFloat pickedParam;
static dVector pickedForce;
static dVector rayLocalNormal;
static dVector rayWorldNormal;
static dVector rayWorldOrigin;
static dVector attachmentPoint;

static bool isPickedBodyDynamics;
static NewtonBody* pickedBody;
static NewtonApplyForceAndTorque chainForceCallBack; 


//#define USE_PICK_BODY_JOINT

#define SHOW_RAYS_ON_STATIC_BODIES

#define MOUSE_PICK_DAMP			 10.0f
#define MOUSE_PICK_STIFFNESS	 100.0f
//#define MOUSE_PICK_STIFFNESS	 50.0f


// the pick body acceleration is 5 gravities
#define MAX_PICK_ACCEL			(9.8f * 10.0f)


// implement a ray cast pre-filter
static unsigned RayCastPrefilter (const NewtonBody* body,  const NewtonCollision* collision, void* userData)
{
	body;
	userData;
	// ray cannot pick trigger volumes
	return NewtonCollisionIsTriggerVolume(collision) ? 0 : 1;

	// alway cast the primitive 
//	return 1;
}

// implement a ray cast filter
static dFloat RayCastFilter (const NewtonBody* body, const dFloat* normal, int collisionID, void* userData, dFloat intersetParam)
{
	dFloat mass;
	dFloat Ixx;
	dFloat Iyy;
	dFloat Izz;

	userData;
	collisionID;
	NewtonBodyGetMassMatrix (body, &mass, &Ixx, &Iyy, &Izz);
	if (intersetParam <  pickedParam) {
		isPickedBodyDynamics = (mass > 0.0f);
		pickedParam = intersetParam;
		pickedBody = (NewtonBody*)body;
		rayLocalNormal = dVector (normal[0], normal[1], normal[2]);
	}
	return intersetParam;
}



static void PhysicsApplyPickForce (const NewtonBody* body, dFloat timestep, int threadIndex)
{
	dFloat mass;
	dFloat Ixx;
	dFloat Iyy;
	dFloat Izz;
	dVector com;
	dVector veloc;
	dVector omega;
	dMatrix matrix;

	// apply the thew body forces
	if (chainForceCallBack) {
		chainForceCallBack (body, timestep, threadIndex);
	}

	// add the mouse pick penalty force and torque
	NewtonBodyGetVelocity(body, &veloc[0]);

	NewtonBodyGetOmega(body, &omega[0]);
	NewtonBodyGetVelocity(body, &veloc[0]);
	NewtonBodyGetMassMatrix (body, &mass, &Ixx, &Iyy, &Izz);

	dVector force (pickedForce.Scale (mass * MOUSE_PICK_STIFFNESS));
	dVector dampForce (veloc.Scale (MOUSE_PICK_DAMP * mass));
	force -= dampForce;

	NewtonBodyGetMatrix(body, &matrix[0][0]);
	NewtonBodyGetCentreOfMass (body, &com[0]);
	
	// calculate local point relative to center of mass
	dVector point (matrix.RotateVector (attachmentPoint - com));
	dVector torque (point * force);

	dVector torqueDamp (omega.Scale (mass * 0.1f));

	NewtonBodyAddForce (body, &force.m_x);
	NewtonBodyAddTorque (body, &torque.m_x);

	// make sure the body is unfrozen, if it is picked
	NewtonBodySetFreezeState (body, 0);
}


#ifndef USE_PICK_BODY_JOINT
bool MousePick (NewtonWorld* nWorld, const dMOUSE_POINT& mouse1, dInt32 mouseLeftKey1, dFloat witdh, dFloat length)
{
	static int mouseLeftKey0;
	static dMOUSE_POINT mouse0;
	static bool mousePickMode = false;
	dMatrix matrix;

	witdh;

	if (mouseLeftKey1) {
		if (!mouseLeftKey0) {
			dVector p0 (ScreenToWorld(dVector (dFloat (mouse1.x), dFloat (mouse1.y), 0.0f, 0.0f)));
			dVector p1 (ScreenToWorld(dVector (dFloat (mouse1.x), dFloat (mouse1.y), 1.0f, 0.0f)));

			pickedBody = NULL;
			pickedParam = 1.1f;
			isPickedBodyDynamics = false;
			NewtonWorldRayCast(nWorld, &p0[0], &p1[0], RayCastFilter, NULL, RayCastPrefilter);
			if (pickedBody) {
				mousePickMode = true;
				//NewtonBodySetFreezeState (pickedBody, 0);
				NewtonBodyGetMatrix(pickedBody, &matrix[0][0]);
				dVector p (p0 + (p1 - p0).Scale (pickedParam));

				// save point local to th body matrix
				attachmentPoint = matrix.UntransformVector (p);

				// convert normal to local space
				rayLocalNormal = matrix.UnrotateVector(rayLocalNormal);

				// save the transform call back
				chainForceCallBack = NewtonBodyGetForceAndTorqueCallback (pickedBody);
				_ASSERTE (chainForceCallBack != PhysicsApplyPickForce);

				// set a new call back
				NewtonBodySetForceAndTorqueCallback (pickedBody, PhysicsApplyPickForce);
			}
		}

		if (mousePickMode) {
			// init pick mode
			dMatrix matrix;
			NewtonBodyGetMatrix(pickedBody, &matrix[0][0]);
			dVector p0 (ScreenToWorld(dVector (dFloat (mouse1.x), dFloat (mouse1.y), 0.0f, 0.0f)));
			dVector p1 (ScreenToWorld(dVector (dFloat (mouse1.x), dFloat (mouse1.y), 1.0f, 0.0f)));
			dVector p2 (matrix.TransformVector (attachmentPoint));

			dVector p (p0 + (p1 - p0).Scale (pickedParam));
			pickedForce = p - p2;
			dFloat mag2 = pickedForce % pickedForce;
			if (mag2 > dFloat (20 * 20)) {
				pickedForce = pickedForce.Scale (20.0f / dSqrt (pickedForce % pickedForce));
			}


			// rotate normal to global space
			rayWorldNormal = matrix.RotateVector(rayLocalNormal);
//			rayWorldOrigin = p2;

			// show the pick points
			//ShowMousePicking (p, p2, witdh);
			ShowMousePicking (p, p2);
			//ShowMousePicking (p2, p2 + rayWorldNormal.Scale (length), witdh);
			ShowMousePicking (p2, p2 + rayWorldNormal.Scale (length));
		}
	} else {
		mousePickMode = false;
		if (pickedBody) {
			//_ASSERTE (chainForceCallBack != NewtonBodyGetForceAndTorqueCallback (pickedBody));
			_ASSERTE (chainForceCallBack != PhysicsApplyPickForce);
			NewtonBodySetForceAndTorqueCallback (pickedBody, chainForceCallBack);
			pickedBody = NULL;
			chainForceCallBack = NULL;
		}
	}

	mouse0 = mouse1;
	mouseLeftKey0 = mouseLeftKey1;

	bool retState;
	retState = isPickedBodyDynamics;
	return retState;
}

#else 

bool MousePick (NewtonWorld* nWorld, const dMOUSE_POINT& mouse1, dInt32 mouseLeftKey1, dFloat witdh, dFloat length)
{

	static int mouseLeftKey0;
	static dMOUSE_POINT mouse0;
	static bool mousePickMode = false;
	dMatrix matrix;
	static NewtonUserJoint* bodyPickController;

	if (mouseLeftKey1) {
		if (!mouseLeftKey0) {
			dVector p0 (ScreenToWorld(dVector (dFloat (mouse1.x), dFloat (mouse1.y), 0.0f, 0.0f)));
			dVector p1 (ScreenToWorld(dVector (dFloat (mouse1.x), dFloat (mouse1.y), 1.0f, 0.0f)));

			pickedBody = NULL;
			pickedParam = 1.1f;
			isPickedBodyDynamics = false;
			NewtonWorldRayCast(nWorld, &p0[0], &p1[0], RayCastFilter, NULL, RayCastPrefilter);
			if (pickedBody) {

				dFloat Ixx;
				dFloat Iyy;
				dFloat Izz;
				dFloat mass;

				mousePickMode = true;
				//NewtonBodySetFreezeState (pickedBody, 0);
				NewtonBodyGetMatrix(pickedBody, &matrix[0][0]);
				dVector p (p0 + (p1 - p0).Scale (pickedParam));

				attachmentPoint = matrix.UntransformVector (p);

				// convert normal to local space
				rayLocalNormal = matrix.UnrotateVector(rayLocalNormal);

				// Create PickBody Joint
				NewtonBodyGetMassMatrix (pickedBody, &mass, &Ixx, &Iyy, &Izz);
				if (mass) {
//					bodyPickController = new CustomPickBody (pickedBody, p);
//					bodyPickController->SetMaxLinearFriction (MAX_PICK_ACCEL);
//					bodyPickController->SetMaxAngularFriction (MAX_PICK_ACCEL * 5.0f);

					bodyPickController = CreateCustomKinematicController (pickedBody, &p[0]);
					CustomKinematicControllerSetMaxLinearFriction (bodyPickController, MAX_PICK_ACCEL);
					CustomKinematicControllerSetMaxAngularFriction (bodyPickController, MAX_PICK_ACCEL * 5.0f);
				}
			}
		}

		if (mousePickMode) {
			// init pick mode
			dVector p0 (ScreenToWorld(dVector (dFloat (mouse1.x), dFloat (mouse1.y), 0.0f, 0.0f)));
			dVector p1 (ScreenToWorld(dVector (dFloat (mouse1.x), dFloat (mouse1.y), 1.0f, 0.0f)));
			dVector p (p0 + (p1 - p0).Scale (pickedParam));
			if (bodyPickController) {
				//bodyPickController->SetTargetPosit (p);
				CustomKinematicControllerSetTargetPosit (bodyPickController, &p[0]);
			}

			// rotate normal to global space
			dMatrix matrix;
			NewtonBodyGetMatrix(pickedBody, &matrix[0][0]);
			rayWorldNormal = matrix.RotateVector(rayLocalNormal);

			dVector p2 (matrix.TransformVector (attachmentPoint));
			ShowMousePicking (p, p2, witdh);
			ShowMousePicking (p2, p2 + rayWorldNormal.Scale (length), witdh);
		}
	} else {
		mousePickMode = false;
		if (pickedBody) {
			if (bodyPickController) {
				//delete bodyPickController;
				CustomDestroyJoint (bodyPickController);
			}
			bodyPickController = NULL;
		}
	}


	mouse0 = mouse1;
	mouseLeftKey0 = mouseLeftKey1;

	bool retState;
	retState = isPickedBodyDynamics;
	return retState;
}

#endif



