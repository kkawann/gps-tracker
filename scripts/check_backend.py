"""Offline backend import/schema/auth smoke checks; no live services are contacted."""
import os
from pathlib import Path
import secrets
import sys

root = Path(__file__).resolve().parents[1]
os.environ.update(
    DATABASE_URL="postgresql+asyncpg://validation:validation@127.0.0.1:5432/validation",
    API_KEY=secrets.token_hex(32),
    JWT_SECRET=secrets.token_hex(32),
)
sys.path.insert(0, str(root / "server/backend"))

import main
from auth import create_access_token, hash_password, verify_password
from config import get_settings
from jose import jwt
from sqlalchemy.orm import configure_mappers

configure_mappers()
schema = main.app.openapi()
assert "/api/auth/login" in schema["paths"]
assert "/api/devices/{device_uid}/locations" in schema["paths"]
assert "/health" in schema["paths"]
assert len(schema["paths"]) >= 30

password = secrets.token_hex(10)
hashed = hash_password(password)
assert verify_password(password, hashed)
assert not verify_password(password + "invalid", hashed)
token = create_access_token({"sub": "offline-validation"})
settings = get_settings()
assert jwt.decode(token, settings.JWT_SECRET, algorithms=[settings.JWT_ALGORITHM])["sub"] == "offline-validation"
print(f"Backend import, ORM mapping, {len(schema['paths'])} OpenAPI paths, password and JWT checks passed.")
