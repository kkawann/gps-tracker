from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from database import get_db
from models import User, Device
from schemas import UserLogin, TokenResponse, OtpRequest, OtpVerify
from auth import verify_password, create_access_token, get_current_user
from mqtt_handler import mqtt_handler
from config import get_settings

settings = get_settings()
router = APIRouter(prefix="/api/auth", tags=["Auth"])


# ─── Username/Password Login (for admin users) ──────────
@router.post("/login", response_model=TokenResponse)
async def login(data: UserLogin, db: AsyncSession = Depends(get_db)):
    result = await db.execute(select(User).where(User.username == data.username))
    user = result.scalar_one_or_none()
    if not user or not verify_password(data.password, user.password_hash):
        raise HTTPException(status_code=401, detail="Invalid credentials")
    if not user.is_active:
        raise HTTPException(status_code=403, detail="Account disabled")
    token = create_access_token({"sub": str(user.id), "username": user.username, "role": user.role})
    return TokenResponse(access_token=token, user_id=user.id, username=user.username)


# ─── OTP Login (via ESP32 SMS) ──────────────────────────
@router.post("/request-otp")
async def request_otp(data: OtpRequest, db: AsyncSession = Depends(get_db)):
    result = await db.execute(select(Device).where(Device.device_uid == data.device_uid))
    device = result.scalar_one_or_none()
    if not device:
        # Fresh install: app registers only AFTER otp verify → auto-create
        # placeholder row (owner=None) so first-time OTP works.
        device = Device(
            device_uid=data.device_uid,
            name=f"GPS {data.device_uid[:12]}",
            mqtt_topic=f"gps/{data.device_uid}",
        )
        db.add(device)
        await db.commit()
        await db.refresh(device)

    if not mqtt_handler._connected:
        raise HTTPException(status_code=503, detail="MQTT disconnected")

    success = mqtt_handler.publish_auth_req(data.device_uid, data.phone)
    if not success:
        raise HTTPException(status_code=503, detail="Failed to send OTP request")

    return {"success": True, "message": "OTP request sent to device"}


@router.post("/verify-otp", response_model=TokenResponse)
async def verify_otp(data: OtpVerify, db: AsyncSession = Depends(get_db)):
    result = await db.execute(select(Device).where(Device.device_uid == data.device_uid))
    device = result.scalar_one_or_none()
    if not device:
        raise HTTPException(status_code=404, detail="Device not found")

    if not mqtt_handler._connected:
        raise HTTPException(status_code=503, detail="MQTT disconnected")

    # Use the phone number from request-otp step (stored in pending_auth)
    phone = mqtt_handler.get_pending_auth_phone(data.device_uid)
    if not phone:
        raise HTTPException(status_code=400, detail="No pending OTP request. Please request a new code.")

    future = mqtt_handler.publish_auth_verify(data.device_uid, phone, data.code)
    try:
        verified = await future
    except Exception:
        verified = False

    if not verified:
        raise HTTPException(status_code=401, detail="Invalid or expired OTP code")

    # Create/find user for this phone
    user_result = await db.execute(select(User).where(User.phone == phone))
    user = user_result.scalar_one_or_none()
    if not user:
        user = User(
            username=f"phone_{phone}",
            email=f"{phone}@gps.local",
            password_hash="otp-login",
            phone=phone,
            full_name=f"User {phone}",
            role="user",
        )
        db.add(user)
        await db.commit()
        await db.refresh(user)

    # Clean up pending auth
    mqtt_handler._pending_auth.pop(data.device_uid, None)

    token = create_access_token({"sub": str(user.id), "username": user.username, "role": user.role})
    return TokenResponse(access_token=token, user_id=user.id, username=user.username)


# ─── Check Current Session ──────────────────────────────
@router.post("/logout")
async def logout(user: User = Depends(get_current_user)):
    """Logout (client should delete token)."""
    return {"success": True, "message": "Logged out"}


@router.post("/refresh", response_model=TokenResponse)
async def refresh_token(user: User = Depends(get_current_user)):
    """Refresh access token (get new token before old one expires)."""
    token = create_access_token({"sub": str(user.id), "username": user.username, "role": user.role})
    return TokenResponse(access_token=token, user_id=user.id, username=user.username)


@router.get("/check")
async def auth_check(user: User = Depends(get_current_user)):
    return {
        "success": True,
        "user_id": str(user.id),
        "username": user.username,
        "role": user.role,
        "is_admin": user.role == "admin",
    }
