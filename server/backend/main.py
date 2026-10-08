import asyncio
import logging
from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from config import get_settings
from database import init_db
from mqtt_handler import mqtt_handler

from routes_auth import router as auth_router
from routes_devices import router as devices_router
from routes_trips import router as trips_router
from routes_geofences import router as geofences_router
from routes_geofence_app import router as geofence_app_router
from routes_gps_geofences import router as gps_geofences_router
from routes_admin import router as admin_router

settings = get_settings()

logging.basicConfig(
    level=logging.DEBUG if settings.DEBUG else logging.INFO,
    format="%(asctime)s [%(name)s] %(levelname)s: %(message)s",
)
logger = logging.getLogger("gps.server")


@asynccontextmanager
async def lifespan(app: FastAPI):
    logger.info(f"Starting {settings.APP_NAME} v{settings.APP_VERSION}")
    await init_db()
    logger.info("Database initialized")
    loop = asyncio.get_event_loop()
    mqtt_handler.start(loop)
    logger.info("MQTT handler started")
    yield
    mqtt_handler.stop()
    logger.info("Server stopped")


app = FastAPI(
    title=settings.APP_NAME,
    version=settings.APP_VERSION,
    description="GPS Tracker Server - MQTT + PostgreSQL + FastAPI",
    lifespan=lifespan,
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

app.include_router(auth_router)
app.include_router(devices_router)
app.include_router(trips_router)
app.include_router(geofences_router)
app.include_router(geofence_app_router)
app.include_router(gps_geofences_router)
app.include_router(admin_router)


@app.get("/")
async def root():
    return {"name": settings.APP_NAME, "version": settings.APP_VERSION, "status": "running", "docs": "/docs"}


@app.get("/health")
async def health():
    return {"status": "healthy"}


if __name__ == "__main__":
    import uvicorn
    uvicorn.run("main:app", host="0.0.0.0", port=8000, reload=settings.DEBUG)
