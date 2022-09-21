
class D3DXMATRIX
{
public:
	float _11, _12, _13, _14;
	float _21, _22, _23, _24;
	float _31, _32, _33, _34;
	float _41, _42, _43, _44;
};

class D3DXVECTOR2
{
public:
	float x, y;
	D3DXVECTOR2() = default;
	D3DXVECTOR2(float x, float y)
	{
		this->x = x;
		this->y = y;
	}
};

class D3DXVECTOR3
{
public:
	float x, y, z;
	D3DXVECTOR3() = default;
	D3DXVECTOR3(float x, float y, float z)
	{
		this->x = x;
		this->y = y;
		this->z = z;
	}
	D3DXVECTOR3 operator +(const D3DXVECTOR3 &v) const
	{
		return D3DXVECTOR3(x + v.x, y + v.y, z + v.z);
	}
};

class D3DXVECTOR4
{
public:
	float x, y, z, w;
	D3DXVECTOR4() = default;
	D3DXVECTOR4(float x, float y, float z, float w)
	{
		this->x = x;
		this->y = y;
		this->z = z;
		this->w = w;
	}
};

static D3DXVECTOR3 *xD3DXVec3Cross(D3DXVECTOR3 *pOut, const D3DXVECTOR3 *A, const D3DXVECTOR3 *B)
{
	pOut->x = A->y * B->z - B->y * A->z;
	pOut->y = A->z * B->x - B->z * A->x;
	pOut->z = A->x * B->y - B->x * A->y;
	return pOut;
}

static D3DXVECTOR3 *xD3DXVec3TransformCoord(D3DXVECTOR3 * pOut, const D3DXVECTOR3 * pV, const D3DXMATRIX * pM)
{
	pOut->x = pV->x * pM->_11 + pV->y * pM->_21 + pV->z * pM->_31 + pM->_41;
	pOut->y = pV->x * pM->_12 + pV->y * pM->_22 + pV->z * pM->_32 + pM->_42;
	pOut->z = pV->x * pM->_13 + pV->y * pM->_23 + pV->z * pM->_33 + pM->_43;
	float w = pV->x * pM->_14 + pV->y * pM->_24 + pV->z * pM->_34 + pM->_44;

	pOut->x /= w;
	pOut->y /= w;
	pOut->z /= w;

	return pOut;
}

static D3DXVECTOR3 *xD3DXVec3Normalize(D3DXVECTOR3 *pOut)
{
	float length = pOut->x * pOut->x + pOut->z * pOut->z + pOut->y * pOut->y;

	if (length > 0.0f) {
		length = sqrtf(length);
		pOut->x /= length;
		pOut->y /= length;
		pOut->z /= length;
		return pOut;
	} else {
		return pOut;
	}
}

static D3DXVECTOR3 *xD3DXVec3Normalize(D3DXVECTOR3 *pOut, const D3DXVECTOR3 *pV)
{
	float length = pV->x * pV->x + pV->z * pV->z + pV->y * pV->y;

	if (length > 0.0f) {
		length = sqrtf(length);
		pOut->x = pV->x / length;
		pOut->y = pV->y / length;
		pOut->z = pV->z / length;
		return pOut;
	} else {
		return pOut;
	}
}

static float xD3DXVec3Dot(const D3DXVECTOR3 *a, const D3DXVECTOR3 *b)
{
	return a->x * b->x + a->y * b->y + a->z * b->z;
}

static D3DXMATRIX* xD3DXMatrixLookAtLH(D3DXMATRIX *pOut, const D3DXVECTOR3 *pEye, const D3DXVECTOR3 *pAt, const D3DXVECTOR3 *pUp)
{
	D3DXVECTOR3 x, y, z;
	z.x = pAt->x - pEye->x;
	z.y = pAt->y - pEye->y;
	z.z = pAt->z - pEye->z;

	xD3DXVec3Normalize(&z);

	xD3DXVec3Cross(&x, pUp, &z);
	xD3DXVec3Normalize(&x);

	xD3DXVec3Cross(&y, &z, &x);

	pOut->_11 = x.x;
	pOut->_12 = y.x;
	pOut->_13 = z.x;
	pOut->_14 = 0.0f;

	pOut->_21 = x.y;
	pOut->_22 = y.y;
	pOut->_23 = z.y;
	pOut->_24 = 0.0f;

	pOut->_31 = x.z;
	pOut->_32 = y.z;
	pOut->_33 = z.z;
	pOut->_34 = 0.0f;

	pOut->_41 = -xD3DXVec3Dot(&x, pEye);
	pOut->_42 = -xD3DXVec3Dot(&y, pEye);
	pOut->_43 = -xD3DXVec3Dot(&z, pEye);
	pOut->_44 = 1.0f;

	return pOut;
}
static D3DXMATRIX *xD3DXMatrixTranspose(D3DXMATRIX *pOut, const D3DXMATRIX *pM)
{
	pOut->_11 = pM->_11;
	pOut->_12 = pM->_21;
	pOut->_13 = pM->_31;
	pOut->_14 = pM->_41;

	pOut->_21 = pM->_12;
	pOut->_22 = pM->_22;
	pOut->_23 = pM->_32;
	pOut->_24 = pM->_42;

	pOut->_31 = pM->_13;
	pOut->_32 = pM->_23;
	pOut->_33 = pM->_33;
	pOut->_34 = pM->_43;

	pOut->_41 = pM->_14;
	pOut->_42 = pM->_24;
	pOut->_43 = pM->_34;
	pOut->_44 = pM->_44;

	return pOut;
}

static D3DXMATRIX *xD3DXMatrixIdentity(D3DXMATRIX *pOut)
{
	pOut->_11 = 1.0f;
	pOut->_12 = 0.0f;
	pOut->_13 = 0.0f;
	pOut->_14 = 0.0f;

	pOut->_21 = 0.0f;
	pOut->_22 = 1.0f;
	pOut->_23 = 0.0f;
	pOut->_24 = 0.0f;

	pOut->_31 = 0.0f;
	pOut->_32 = 0.0f;
	pOut->_33 = 1.0f;
	pOut->_34 = 0.0f;

	pOut->_41 = 0.0f;
	pOut->_42 = 0.0f;
	pOut->_43 = 0.0f;
	pOut->_44 = 1.0f;

	return pOut;
}

static D3DXMATRIX *xD3DXMatrixOrthoLH(D3DXMATRIX *pOut, float w, float h, float zn, float zf)
{
	float d = zf - zn;

	xD3DXMatrixIdentity(pOut);
	pOut->_11 = 2.0f / float(w);
	pOut->_22 = 2.0f / float(h);
	pOut->_33 = 1.0f / d;
	pOut->_43 = -zn / d;

	return pOut;
}

static D3DXMATRIX *xD3DXMatrixMultiply(D3DXMATRIX * pOut, const D3DXMATRIX * pM1, const D3DXMATRIX * pM2)
{
	if (pOut == NULL) {
		pOut = xD3DXMatrixIdentity(pOut);
	}

	pOut->_11 = pM1->_11 * pM2->_11 + pM1->_12 * pM2->_21 + pM1->_13 * pM2->_31 + pM1->_14 * pM2->_41;
	pOut->_12 = pM1->_11 * pM2->_12 + pM1->_12 * pM2->_22 + pM1->_13 * pM2->_32 + pM1->_14 * pM2->_42;
	pOut->_13 = pM1->_11 * pM2->_13 + pM1->_12 * pM2->_23 + pM1->_13 * pM2->_33 + pM1->_14 * pM2->_43;
	pOut->_14 = pM1->_11 * pM2->_14 + pM1->_12 * pM2->_24 + pM1->_13 * pM2->_34 + pM1->_14 * pM2->_44;
	pOut->_21 = pM1->_21 * pM2->_11 + pM1->_22 * pM2->_21 + pM1->_23 * pM2->_31 + pM1->_24 * pM2->_41;
	pOut->_22 = pM1->_21 * pM2->_12 + pM1->_22 * pM2->_22 + pM1->_23 * pM2->_32 + pM1->_24 * pM2->_42;
	pOut->_23 = pM1->_21 * pM2->_13 + pM1->_22 * pM2->_23 + pM1->_23 * pM2->_33 + pM1->_24 * pM2->_43;
	pOut->_24 = pM1->_21 * pM2->_14 + pM1->_22 * pM2->_24 + pM1->_23 * pM2->_34 + pM1->_24 * pM2->_44;
	pOut->_31 = pM1->_31 * pM2->_11 + pM1->_32 * pM2->_21 + pM1->_33 * pM2->_31 + pM1->_34 * pM2->_41;
	pOut->_32 = pM1->_31 * pM2->_12 + pM1->_32 * pM2->_22 + pM1->_33 * pM2->_32 + pM1->_34 * pM2->_42;
	pOut->_33 = pM1->_31 * pM2->_13 + pM1->_32 * pM2->_23 + pM1->_33 * pM2->_33 + pM1->_34 * pM2->_43;
	pOut->_34 = pM1->_31 * pM2->_14 + pM1->_32 * pM2->_24 + pM1->_33 * pM2->_34 + pM1->_34 * pM2->_44;
	pOut->_41 = pM1->_41 * pM2->_11 + pM1->_42 * pM2->_21 + pM1->_43 * pM2->_31 + pM1->_44 * pM2->_41;
	pOut->_42 = pM1->_41 * pM2->_12 + pM1->_42 * pM2->_22 + pM1->_43 * pM2->_32 + pM1->_44 * pM2->_42;
	pOut->_43 = pM1->_41 * pM2->_13 + pM1->_42 * pM2->_23 + pM1->_43 * pM2->_33 + pM1->_44 * pM2->_43;
	pOut->_44 = pM1->_41 * pM2->_14 + pM1->_42 * pM2->_24 + pM1->_43 * pM2->_34 + pM1->_44 * pM2->_44;

	return pOut;
}

static D3DXMATRIX *xD3DXMatrixRotationX(D3DXMATRIX * pOut, float Angle)
{
	if (pOut == NULL) {
		pOut = xD3DXMatrixIdentity(pOut);
	} else {
		xD3DXMatrixIdentity(pOut);
	}

	float cosAng = cosf(Angle);
	float sinAng = sinf(Angle);

	pOut->_22 = cosAng;
	pOut->_33 = cosAng;
	pOut->_23 = sinAng;
	pOut->_32 = -sinAng;

	return pOut;
}

static D3DXMATRIX *xD3DXMatrixRotationY(D3DXMATRIX * pOut, float Angle)
{
	if (pOut == NULL) {
		pOut = xD3DXMatrixIdentity(pOut);
	} else {
		xD3DXMatrixIdentity(pOut);
	}

	float cosAng = cosf(Angle);
	float sinAng = sinf(Angle);

	pOut->_11 = cosAng;
	pOut->_33 = cosAng;
	pOut->_13 = -sinAng;
	pOut->_31 = sinAng;

	return pOut;
}

static D3DXMATRIX *xD3DXMatrixRotationZ(D3DXMATRIX * pOut, float Angle)
{
	if (pOut == NULL) {
		pOut = xD3DXMatrixIdentity(pOut);
	} else {
		xD3DXMatrixIdentity(pOut);
	}

	float cosAng = cosf(Angle);
	float sinAng = sinf(Angle);

	pOut->_11 = cosAng;
	pOut->_22 = cosAng;
	pOut->_12 = sinAng;
	pOut->_21 = -sinAng;

	return pOut;
}

static D3DXMATRIX *xD3DXMatrixRotationYawPitchRoll(D3DXMATRIX *pout, FLOAT yaw, FLOAT pitch, FLOAT roll) {
	D3DXMATRIX m, pout1, pout2, pout3;

	xD3DXMatrixIdentity(&pout3);
	xD3DXMatrixRotationZ(&m, roll);
	xD3DXMatrixMultiply(&pout2, &pout3, &m);
	xD3DXMatrixRotationX(&m, pitch);
	xD3DXMatrixMultiply(&pout1, &pout2, &m);
	xD3DXMatrixRotationY(&m, yaw);
	xD3DXMatrixMultiply(pout, &pout1, &m);
	return pout;
}

static D3DXMATRIX* xD3DXMatrixOrthoOffCenterLH(D3DXMATRIX *pOut, float l, float r, float b, float t, float zn, float zf)
{
	pOut->_11 = 2.0f / r; pOut->_12 = 0.0f;   pOut->_13 = 0.0f;  pOut->_14 = 0.0f;
	pOut->_21 = 0.0f;   pOut->_22 = 2.0f / t; pOut->_23 = 0.0f;  pOut->_24 = 0.0f;
	pOut->_31 = 0.0f;   pOut->_32 = 0.0f;   pOut->_33 = 1.0f;  pOut->_34 = 0.0f;
	pOut->_41 = -1.0f;  pOut->_42 = -1.0f;  pOut->_43 = 0.0f;  pOut->_44 = 1.0f;
	return pOut;
}

static D3DXMATRIX* xD3DXMatrixScaling(D3DXMATRIX *pOut, float sx, float sy, float sz)
{
	pOut->_11 = sx;     pOut->_12 = 0.0f;   pOut->_13 = 0.0f;  pOut->_14 = 0.0f;
	pOut->_21 = 0.0f;   pOut->_22 = sy;     pOut->_23 = 0.0f;  pOut->_24 = 0.0f;
	pOut->_31 = 0.0f;   pOut->_32 = 0.0f;   pOut->_33 = sz;    pOut->_34 = 0.0f;
	pOut->_41 = 0.0f;   pOut->_42 = 0.0f;   pOut->_43 = 0.0f;  pOut->_44 = 1.0f;
	return pOut;
}

static D3DXMATRIX* xD3DXMatrixTranslation(D3DXMATRIX *pOut, float tx, float ty, float tz)
{
	pOut->_11 = 1.0f;   pOut->_12 = 0.0f;   pOut->_13 = 0.0f;  pOut->_14 = 0.0f;
	pOut->_21 = 0.0f;   pOut->_22 = 1.0f;   pOut->_23 = 0.0f;  pOut->_24 = 0.0f;
	pOut->_31 = 0.0f;   pOut->_32 = 0.0f;   pOut->_33 = 1.0f;  pOut->_34 = 0.0f;
	pOut->_41 = tx;     pOut->_42 = ty;     pOut->_43 = tz;    pOut->_44 = 1.0f;
	return pOut;
}