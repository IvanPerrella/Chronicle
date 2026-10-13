@echo off
setlocal
set "ROOT=%~dp0"
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=windows-x64-release"
where docker >nul 2>nul || (echo Docker Desktop is required. 1>&2 & exit /b 1)
docker info >nul 2>nul || (echo Start Docker Desktop's Linux engine and try again. 1>&2 & exit /b 1)
docker build --file "%ROOT%tools\windows\Dockerfile" --tag chronicle-windows-build:20260922 "%ROOT%tools\windows" || exit /b 1
docker run --rm --mount "type=bind,source=%ROOT%,target=/src" --env CLEAN --env JOBS chronicle-windows-build:20260922 "%CONFIG%"
exit /b %ERRORLEVEL%
