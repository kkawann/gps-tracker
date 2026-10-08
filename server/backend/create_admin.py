"""Bootstrap an administrator with an interactive password, without default seeds."""
import asyncio
from getpass import getpass

from sqlalchemy import select

from auth import hash_password
from database import async_session, engine, init_db
from models import User


async def create(username, email, password):
    try:
        await init_db()
        async with async_session() as session:
            existing = await session.execute(
                select(User).where((User.username == username) | (User.email == email))
            )
            if existing.scalar_one_or_none():
                raise SystemExit("Username or email already exists; no account changed.")
            session.add(User(
                username=username,
                email=email,
                password_hash=hash_password(password),
                role="admin",
                is_active=True,
            ))
            await session.commit()
            print("Administrator created.")
    finally:
        await engine.dispose()


if __name__ == "__main__":
    username = input("Admin username: ").strip()
    email = input("Admin email: ").strip()
    password = getpass("New admin password: ")
    confirmation = getpass("Repeat password: ")
    if not username or not email or len(password) < 12:
        raise SystemExit("Supply a username, email and password of at least 12 characters.")
    if password != confirmation:
        raise SystemExit("Passwords did not match; no account created.")
    asyncio.run(create(username, email, password))
