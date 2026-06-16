@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul

echo ====================================================================
echo   Portable Git ^& GitHub Push Helper [Kein Admin-Zugang noetig]
echo ====================================================================
echo.

set "WORK_DIR=%~dp0"
set "PORTABLE_GIT_EXE=%WORK_DIR%PortableGit.exe"
set "PORTABLE_GIT_DIR=%WORK_DIR%PortableGit"

:: Schritt 1: Pruefen ob Portable Git bereits vorhanden ist
if not exist "%PORTABLE_GIT_DIR%\cmd\git.exe" (
    echo [1/3] Lade Portable Git herunter [ca. 45 MB]...
    powershell -Command "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -Uri 'https://github.com/git-for-windows/git/releases/download/v2.43.0.windows.1/PortableGit-2.43.0-64-bit.7z.exe' -OutFile '%PORTABLE_GIT_EXE%'"
    if errorlevel 1 (
        echo.
        echo [FEHLER] Download von Git fehlgeschlagen!
        pause
        exit /b
    )

    echo [2/3] Entpacke Git...
    "%PORTABLE_GIT_EXE%" -y -o"%PORTABLE_GIT_DIR%"
    if errorlevel 1 (
        echo.
        echo [FEHLER] Entpacken fehlgeschlagen!
        pause
        exit /b
    )

    del "%PORTABLE_GIT_EXE%"
    echo.
) else (
    echo [Info] Portable Git bereits vorhanden.
)

:: Pfad auf das portable Git setzen
set "PATH=%PORTABLE_GIT_DIR%\cmd;%PATH%"

:: Schritt 2: Pruefen ob ein Git-Repository initialisiert ist
if not exist "%WORK_DIR%.git" (
    echo [Info] Initialisiere neues Git-Repository...
    git init
)

:: Remote URL ueberpruefen oder anlegen
git remote get-url origin >nul 2>&1
if errorlevel 1 (
    echo.
    echo Bitte erstelle ein leeres Repository auf github.com [z.B. gd3d11].
    set /p "REPO_URL=Gib hier die HTTPS-URL deines Repositories ein [z.B. https://github.com/DEIN_NAME/gd3d11.git]: "
    git remote add origin !REPO_URL!
) else (
    for /f "tokens=*" %%i in ('git remote get-url origin') do set "CURRENT_URL=%%i"
    echo [Info] Aktuelles GitHub Repository: !CURRENT_URL!
    echo Moechtest du diese URL aendern? [J = Ja, N = Nein]
    set /p "CHANGE_URL=Eingabe (J/N): "
    if "!CHANGE_URL!"=="J" (
        set /p "REPO_URL=Gib die neue HTTPS-URL ein: "
        git remote set-url origin !REPO_URL!
    )
    if "!CHANGE_URL!"=="j" (
        set /p "REPO_URL=Gib die neue HTTPS-URL ein: "
        git remote set-url origin !REPO_URL!
    )
)

:: Git Benutzername/E-Mail konfigurieren [wird fuer Commits benoetigt]
git config --global user.name "Gothic Developer"
git config --global user.email "gothic@localhost"

:: Zweig-Namen abrufen [Master oder Main]
for /f "tokens=*" %%i in ('git branch --show-current') do set "BRANCH=%%i"
if "!BRANCH!"=="" set "BRANCH=master"

:: Schritt 3: Committen ^& Pushen
echo.
echo [3/3] Committe Aenderungen...
git add -A
git commit -m "Implement SSS and SSR features and compile fixes"

echo.
echo ====================================================================
echo   Pushe zu GitHub...
echo   Gleich oeffnet sich ein Windows-Fenster zur Anmeldung.
echo   Klicke dort einfach auf "Sign in with your browser".
echo ====================================================================
echo.

git push -f -u origin !BRANCH!

if errorlevel 1 (
    echo.
    echo [FEHLER] Push fehlgeschlagen!
    echo Bitte stelle sicher, dass das Repository existiert und deine Zugangsdaten stimmen.
) else (
    echo.
    echo ====================================================================
    echo   ERFOLG! Deine geaenderten Dateien sind jetzt auf GitHub.
    echo   GitHub Actions baut nun deine DLLs im Hintergrund in der Cloud.
    echo   Nach 2-3 Minuten kannst du die fertigen DLLs unter "Actions" laden!
    echo ====================================================================
)

echo.
pause
