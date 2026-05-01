#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
#else
#include <unistd.h>
#endif

#include "ShaderEditor.h"
#include "Renderer.h"
#include "FFT.h"
#include "MIDI.h"
#include "Timer.h"
#include "Misc.h"
#include "UniConversion.h"
#include "jsonxx.h"
#include "Capture.h"
#include "Network.h"
#include "NetworkSettings.h"
#include "Cmdline.h"

unsigned int ParseColor(const std::string& color) {
  if (color.size() < 6 || color.size() > 8) return 0xFFFFFFFF;
  if (color.size() == 6)
  {
    std::string text = "0x" + color;
    unsigned int v = std::stoul(text, 0, 16);
    return (0xFF000000 | ((v & 0xFF0000) >> 16) | (v & 0x00FF00) | ((v & 0x0000FF) << 16));
  }
  else
  {
    std::string text = "0x" + color;
    unsigned int v = std::stoul(text, 0, 16);
    return ((v & 0xFF000000) | ((v & 0x00FF0000) >> 16) | (v & 0x0000FF00) | ((v & 0x000000FF) << 16));
  }
}

void ReplaceTokens( std::string &sDefShader, const char * sTokenBegin, const char * sTokenName, const char * sTokenEnd, std::vector<std::string> &tokens )
{
  if (sDefShader.find(sTokenBegin) != std::string::npos
    && sDefShader.find(sTokenName) != std::string::npos
    && sDefShader.find(sTokenEnd) != std::string::npos
    && sDefShader.find(sTokenBegin) < sDefShader.find(sTokenName)
    && sDefShader.find(sTokenName) < sDefShader.find(sTokenEnd))
  {
    int nTokenStart = (int)(sDefShader.find(sTokenBegin) + strlen(sTokenBegin));
    std::string sTextureToken = sDefShader.substr( nTokenStart, sDefShader.find(sTokenEnd) - nTokenStart );

    std::string sFinalShader;
    sFinalShader = sDefShader.substr( 0, sDefShader.find(sTokenBegin) );

    //for (std::map<std::string, Renderer::Texture*>::iterator it = tokens.begin(); it != tokens.end(); it++)
    for (int i=0; i < tokens.size(); i++)
    {
      std::string s = sTextureToken;
      while (s.find(sTokenName) != std::string::npos)
      {
        s.replace( s.find(sTokenName), strlen(sTokenName), tokens[i], 0, std::string::npos );
      }
      sFinalShader += s;
    }
    sFinalShader += sDefShader.substr( sDefShader.find(sTokenEnd) + strlen(sTokenEnd), std::string::npos );
    sDefShader = sFinalShader;
  }
}

bool CmdHasOption(int argc, const char *argv[], const std::string Option, std::string* Parameter) {
  // start from 2nd param, first is exe name
  for (int i = 1; i < argc; ++i) {
    std::string strarg = argv[i];
    size_t foundEqual = strarg.find('=');
    if (foundEqual == std::string::npos) {
      if (strarg == Option) {
        return true;
      }
    }
    else {
      std::string stroption = strarg.substr(0, foundEqual);
      if (stroption == Option) {
        if (Parameter) {
          (*Parameter) = strarg.substr(foundEqual + 1, std::string::npos);
        }
        return true;
      }
    }
  }
  return false;
}

void Network_Break_URL(std::string ServerURL, std::string& ServerName, std::string& RoomName, std::string& NickName) {
  
    std::size_t LastPart = ServerURL.rfind('/');
    if(LastPart == std::string::npos || LastPart<=7) {
      ServerName = ServerURL;
    } else {
      std::size_t SecondPart = ServerURL.rfind('/', LastPart-1);
      if(SecondPart == std::string::npos || SecondPart<=7) {
        ServerName = ServerURL.substr(0,LastPart);
        RoomName = ServerURL.substr(LastPart+1);
      } else {
        ServerName = ServerURL.substr(0,SecondPart);
        RoomName = ServerURL.substr(SecondPart+1, LastPart-SecondPart-1);
        NickName = ServerURL.substr(LastPart+1);
      }
    }
}

int main(int argc, const char *argv[])
{
  Misc::PlatformStartup();

  std::string configFile = "config.json";
  
  if (CmdHasOption(argc, argv, "configfile", &configFile)) {
    printf("Loading config file '%s'...\n", configFile.c_str());
  } else {
    char configPath[256] = { 0 };
    if (getcwd(configPath, 255))
    {
      printf("Looking for config.json in '%s'...\n", configPath);
    }
  }
  
  jsonxx::Object options;
  FILE * fConf = fopen( configFile.c_str(), "rb" );
  if (fConf)
  {
    printf("Config file found, parsing...\n");

    char szConfig[65535];
    memset( szConfig, 0, 65535 );
    fread( szConfig, 1, 65535, fConf );
    fclose(fConf);

    options.parse( szConfig );
  }

  bool SkipConfigDialog = CmdHasOption(argc, argv, "skipdialog");
  
  RENDERER_SETTINGS settings;
  settings.bVsync = false;

  NETWORK_SETTINGS netSettings;
  if (options.has<jsonxx::Object>("network"))
		{
			jsonxx::Object netjson = options.get<jsonxx::Object>("network");
			if (netjson.has<jsonxx::Boolean>("enabled"))
				netSettings.EnableNetwork = netjson.get<jsonxx::Boolean>("enabled");
			if (netjson.has<jsonxx::String>("serverURL"))
				netSettings.ServerURL = netjson.get<jsonxx::String>("serverURL");
			if (netjson.has<jsonxx::String>("networkMode"))
				netSettings.NetworkModeString = netjson.get<jsonxx::String>("networkMode");
		}

  bool fftCapturePlaybackDevices = false;
  std::string fftCaptureDeviceSearchString = "";
  if (options.has<jsonxx::Object>("rendering"))
  {
    jsonxx::Object RenderingSection = options.get<jsonxx::Object>("rendering");
    if (RenderingSection.has<jsonxx::Boolean>("fftCapturePlaybackDevices"))
      fftCapturePlaybackDevices = RenderingSection.get<jsonxx::Boolean>("fftCapturePlaybackDevices");
    if (RenderingSection.has<jsonxx::String>("fftCaptureDeviceSearchString"))
      fftCaptureDeviceSearchString = RenderingSection.get<jsonxx::String>("fftCaptureDeviceSearchString");
  }

//#ifdef _DEBUG
#if 0
  settings.nWidth = 1280;
  settings.nHeight = 720;
  settings.windowMode = RENDERER_WINDOWMODE_WINDOWED;
  settings.ResizeableWindow = true;
#else
  settings.nWidth = 1920;
  settings.nHeight = 1080;
  settings.windowMode = RENDERER_WINDOWMODE_FULLSCREEN;
  settings.ResizableWindow = false;
  if (options.has<jsonxx::Object>("window"))
  {
    jsonxx::Object winjson = options.get<jsonxx::Object>("window");
    if (winjson.has<jsonxx::Number>("width"))
      settings.nWidth = winjson.get<jsonxx::Number>("width");
    if (winjson.has<jsonxx::Number>("height"))
      settings.nHeight = winjson.get<jsonxx::Number>("height");
    if (winjson.has<jsonxx::Boolean>("fullscreen"))
      settings.windowMode = winjson.get<jsonxx::Boolean>("fullscreen") ? RENDERER_WINDOWMODE_FULLSCREEN : RENDERER_WINDOWMODE_WINDOWED;
    if (winjson.has<jsonxx::Boolean>("borderlessfullscreen") && winjson.get<jsonxx::Boolean>("borderlessfullscreen"))
      settings.windowMode = RENDERER_WINDOWMODE_BORDERLESS;
    if (winjson.has<jsonxx::Boolean>("resizable"))
      settings.ResizableWindow = winjson.get<jsonxx::Boolean>("resizable");
    if (winjson.has<jsonxx::Boolean>("hideConsole") && winjson.get<jsonxx::Boolean>("hideConsole"))
      Misc::HideConsoleWindow();
  }
  if(!SkipConfigDialog)
  {
    printf("Waiting Config dialog\n");
    if (!Renderer::OpenSetupDialog( &settings, &netSettings ))
      return -1;
  }
#endif
  
  Network::CommandLine(argc, argv, &netSettings);
  Network::LoadSettings(options, &netSettings);
  Network::PrepareConnection();
  Network::OpenConnection();

  std::string windowName = "";
  if(Network::IsNetworkEnabled()) 
  {
    windowName = " " + Network::GetModeString() + " " + Network::GetNickName();
  }
  if (!Renderer::Open( &settings, windowName))
  {
    printf("Renderer::Open failed\n");
    return -1;
  }

  if (!FFT::Open(fftCapturePlaybackDevices, fftCaptureDeviceSearchString.c_str()))
  {
    printf("FFT::Open() failed, continuing anyway...\n");
    //return -1;
  }

  if (!MIDI::Open())
  {
    printf("MIDI::Open() failed, continuing anyway...\n");
    //return -1;
  }

  std::map<std::string,Renderer::Texture*> textures;
  std::map<int,std::string> midiRoutes;

  jsonxx::Object networkShaderParameters;
  std::map<std::string, Network::ShaderParamCache> networkParamCache;
  
  const char * szDefaultFontPath = Misc::GetDefaultFontPath();

  SHADEREDITOR_OPTIONS editorOptions;
  editorOptions.nFontSize = 16;
  if ( !szDefaultFontPath )
  {
    printf( "Misc::GetDefaultFontPath couldn't find ANY default fonts!\n" );
  }
  else
  {
    editorOptions.sFontPath = szDefaultFontPath;
  }
  editorOptions.nOpacity = 0xC0;
  editorOptions.bUseSpacesForTabs = true;
  editorOptions.nTabSize = 2;
  editorOptions.bVisibleWhitespace = false;
  editorOptions.eAutoIndent = aitSmart;

  int nDebugOutputHeight = 200;
  int nTexPreviewWidth = 64;
  bool bAlwaysDisplayCoderName = false;
  float fFFTSmoothingFactor = 0.9f; // higher value, smoother FFT
  float fFFTSlightSmoothingFactor = 0.6f; // higher value, smoother FFT
  float fScrollXFactor = 1.0f;
  float fScrollYFactor = 1.0f;

  std::string sPostExitCmd;

  if (!options.empty())
  {
    if (options.has<jsonxx::Object>("rendering"))
    {
      jsonxx::Object RenderingSection = options.get<jsonxx::Object>("rendering");
      if (RenderingSection.has<jsonxx::Number>("fftSmoothFactor"))
        fFFTSmoothingFactor = RenderingSection.get<jsonxx::Number>("fftSmoothFactor");
      if (RenderingSection.has<jsonxx::Number>("fftAmplification"))
        FFT::fAmplification = RenderingSection.get<jsonxx::Number>("fftAmplification");
      if (RenderingSection.has<jsonxx::Boolean>("fftPeakNormalization"))
        FFT::bPeakNormalization = RenderingSection.get<jsonxx::Boolean>("fftPeakNormalization");
      if (RenderingSection.has<jsonxx::Number>("fftPeakMinValue"))
        FFT::fPeakMinValue = RenderingSection.get<jsonxx::Number>("fftPeakMinValue");
      if (RenderingSection.has<jsonxx::Number>("fftPeakSmoothing"))
        FFT::fPeakSmoothing = RenderingSection.get<jsonxx::Number>("fftPeakSmoothing");
    }

    if (options.has<jsonxx::Object>("textures"))
    {
      printf("Loading textures...\n");
      std::map<std::string, jsonxx::Value*> tex = options.get<jsonxx::Object>("textures").kv_map();
      for (std::map<std::string, jsonxx::Value*>::iterator it = tex.begin(); it != tex.end(); it++)
      {
        const char * fn = it->second->string_value_->c_str();
        printf("* %s...\n",fn);
        Renderer::Texture * tex = Renderer::CreateRGBA8TextureFromFile( fn );
        if (!tex)
        {
          printf("Renderer::CreateRGBA8TextureFromFile(%s) failed\n",fn);
          return -1;
        }
        textures[it->first] = tex;
      }
    }
    if (options.has<jsonxx::Object>("font"))
    {
      if (options.get<jsonxx::Object>("font").has<jsonxx::Number>("size"))
        editorOptions.nFontSize = options.get<jsonxx::Object>("font").get<jsonxx::Number>("size");
      if (options.get<jsonxx::Object>("font").has<jsonxx::String>("file"))
      {
        std::string fontpath = options.get<jsonxx::Object>("font").get<jsonxx::String>("file");
        if (!Misc::FileExists(fontpath.c_str()))
        {
          printf("Font path (%s) is invalid!\n", fontpath.c_str());
          return -1;
        }
        editorOptions.sFontPath = fontpath;
      }
    }
    editorOptions.sCoderNameFontPath = editorOptions.sFontPath;
    editorOptions.nCoderNameFontSize = editorOptions.nFontSize;
    if (options.has<jsonxx::Object>("codernamefont"))
    {
      if (options.get<jsonxx::Object>("codernamefont").has<jsonxx::Number>("size"))
        editorOptions.nCoderNameFontSize = options.get<jsonxx::Object>("codernamefont").get<jsonxx::Number>("size");
      if (options.get<jsonxx::Object>("codernamefont").has<jsonxx::String>("file"))
      {
        std::string fontpath = options.get<jsonxx::Object>("codernamefont").get<jsonxx::String>("file");
        if (!Misc::FileExists(fontpath.c_str()))
        {
          printf("Font path (%s) for coder is invalid!\n", fontpath.c_str());
          return -1;
        }
        editorOptions.sCoderNameFontPath = fontpath;
      }
    }
    if (options.has<jsonxx::Object>("gui"))
    {
      if (options.get<jsonxx::Object>("gui").has<jsonxx::Number>("outputHeight"))
        nDebugOutputHeight = options.get<jsonxx::Object>("gui").get<jsonxx::Number>("outputHeight");
      if (options.get<jsonxx::Object>("gui").has<jsonxx::Number>("texturePreviewWidth"))
        nTexPreviewWidth = options.get<jsonxx::Object>("gui").get<jsonxx::Number>("texturePreviewWidth");
      if (options.get<jsonxx::Object>("gui").has<jsonxx::Boolean>("alwaysdisplaycodername"))
        bAlwaysDisplayCoderName = options.get<jsonxx::Object>("gui").get<jsonxx::Boolean>("alwaysdisplaycodername");
      if (options.get<jsonxx::Object>("gui").has<jsonxx::Number>("opacity"))
        editorOptions.nOpacity = options.get<jsonxx::Object>("gui").get<jsonxx::Number>("opacity");
      if (options.get<jsonxx::Object>("gui").has<jsonxx::Boolean>("spacesForTabs"))
        editorOptions.bUseSpacesForTabs = options.get<jsonxx::Object>("gui").get<jsonxx::Boolean>("spacesForTabs");
      if (options.get<jsonxx::Object>("gui").has<jsonxx::Number>("tabSize"))
        editorOptions.nTabSize = options.get<jsonxx::Object>("gui").get<jsonxx::Number>("tabSize");
      if (options.get<jsonxx::Object>("gui").has<jsonxx::Boolean>("visibleWhitespace"))
        editorOptions.bVisibleWhitespace = options.get<jsonxx::Object>("gui").get<jsonxx::Boolean>("visibleWhitespace");
      if (options.get<jsonxx::Object>("gui").has<jsonxx::String>("autoIndent"))
      {
        std::string autoIndent = options.get<jsonxx::Object>("gui").get<jsonxx::String>("autoIndent");
        if (autoIndent == "smart") {
          editorOptions.eAutoIndent = aitSmart;
        } else if (autoIndent == "preserve") {
          editorOptions.eAutoIndent = aitPreserve;
        } else {
          editorOptions.eAutoIndent = aitNone;
        }
      }
      if (options.get<jsonxx::Object>("gui").has<jsonxx::Number>("scrollXFactor"))
        fScrollXFactor = options.get<jsonxx::Object>("gui").get<jsonxx::Number>("scrollXFactor");
      if (options.get<jsonxx::Object>("gui").has<jsonxx::Number>("scrollYFactor"))
        fScrollYFactor = options.get<jsonxx::Object>("gui").get<jsonxx::Number>("scrollYFactor");
    }
    if (options.has<jsonxx::Object>("theme"))
    {
      const auto& theme = options.get<jsonxx::Object>("theme");
      if (theme.has<jsonxx::String>("text"))
        editorOptions.theme.text = ParseColor(theme.get<jsonxx::String>("text"));
      if (theme.has<jsonxx::String>("comment"))
        editorOptions.theme.comment = ParseColor(theme.get<jsonxx::String>("comment"));
      if (theme.has<jsonxx::String>("number"))
        editorOptions.theme.number = ParseColor(theme.get<jsonxx::String>("number"));
      if (theme.has<jsonxx::String>("op"))
        editorOptions.theme.op = ParseColor(theme.get<jsonxx::String>("op"));
      if (theme.has<jsonxx::String>("keyword"))
        editorOptions.theme.keyword = ParseColor(theme.get<jsonxx::String>("keyword"));
      if (theme.has<jsonxx::String>("type"))
        editorOptions.theme.type = ParseColor(theme.get<jsonxx::String>("type"));
      if (theme.has<jsonxx::String>("builtin"))
        editorOptions.theme.builtin = ParseColor(theme.get<jsonxx::String>("builtin"));
      if (theme.has<jsonxx::String>("preprocessor"))
        editorOptions.theme.preprocessor = ParseColor(theme.get<jsonxx::String>("preprocessor"));
      if (theme.has<jsonxx::String>("selection"))
        editorOptions.theme.selection = ParseColor(theme.get<jsonxx::String>("selection"));
      if (theme.has<jsonxx::String>("codername"))
        editorOptions.theme.codername = ParseColor(theme.get<jsonxx::String>("codername"));
      if (theme.has<jsonxx::String>("charBackground")) {
        editorOptions.theme.bUseCharBackground = true;
        editorOptions.theme.charBackground = ParseColor(theme.get<jsonxx::String>("charBackground"));
      }
    }
    if (options.has<jsonxx::Object>("midi"))
    {
      std::map<std::string, jsonxx::Value*> tex = options.get<jsonxx::Object>("midi").kv_map();
      for (std::map<std::string, jsonxx::Value*>::iterator it = tex.begin(); it != tex.end(); it++)
      {
        midiRoutes[it->second->number_value_] = it->first;
      }
    }
    if (options.has<jsonxx::String>("postExitCmd"))
    {
      sPostExitCmd = options.get<jsonxx::String>("postExitCmd");
    }
    Capture::LoadSettings( options );
  }
  if (!editorOptions.sFontPath.size())
  {
    printf("Couldn't find any of the default fonts. Please specify one in config.json\n");
    return -1;
  }
  if (!Capture::Open(settings))
  {
    printf("Initializing capture system failed!\n");
    return 0;
  }
  
  Renderer::Texture * texPreviousFrame = Renderer::CreateRGBA8Texture();
  Renderer::Texture * texFFT = Renderer::Create1DR32Texture( FFT_SIZE );
  Renderer::Texture * texFFTSmoothed = Renderer::Create1DR32Texture( FFT_SIZE );
  Renderer::Texture * texFFTIntegrated = Renderer::Create1DR32Texture( FFT_SIZE );

  std::string shaderFileName = Renderer::defaultShaderFilename;
  if (Network::IsNetworkEnabled())
  {
    std::string ServerName;
    std::string RoomName;
    std::string NickName;
    Network_Break_URL(netSettings.ServerURL, ServerName, RoomName, NickName);
    std::string Mode = Network::GetModeString();
    if(RoomName.length()>0) {
      if(NickName.length()>0) {
        shaderFileName = Mode + "_" + RoomName + "_" + NickName + Renderer::defaultShaderExtention;
      } else {
        shaderFileName = Mode + "_" + RoomName + Renderer::defaultShaderExtention;
      }
    }
  }
  if (CmdHasOption(argc, argv, "shader", &shaderFileName)) {
    printf("Loading Shader: %s \n", shaderFileName.c_str());
  }

  bool shaderInitSuccessful = false;
  char szShader[65535];
  char szError[4096];
  FILE * f = fopen(shaderFileName.c_str(),"rb");
  if (f)
  {
    printf("Loading last shader...\n");

    memset( szShader, 0, 65535 );
    fread( szShader, 1, 65535, f );
    fclose(f);
    if (Renderer::ReloadShader( szShader, (int)strlen(szShader), szError, 4096 ))
    {
      printf("Last shader works fine.\n");
      shaderInitSuccessful = true;
    }
    else {
      printf("Shader error:\n%s\n", szError);
    }
  }
  if (!shaderInitSuccessful)
  {
    printf("No valid last shader found, falling back to default...\n");

    std::string sDefShader = Renderer::defaultShader;

    std::vector<std::string> tokens;
    for (std::map<std::string, Renderer::Texture*>::iterator it = textures.begin(); it != textures.end(); it++)
      tokens.push_back(it->first);
    ReplaceTokens(sDefShader, "{%textures:begin%}", "{%textures:name%}", "{%textures:end%}", tokens);

    tokens.clear();
    for (std::map<int,std::string>::iterator it = midiRoutes.begin(); it != midiRoutes.end(); it++)
      tokens.push_back(it->second);
    ReplaceTokens(sDefShader, "{%midi:begin%}", "{%midi:name%}", "{%midi:end%}", tokens);

    strncpy( szShader, sDefShader.c_str(), 65535 );
    if (!Renderer::ReloadShader( szShader, (int)strlen(szShader), szError, 4096 ))
    {
      printf("Default shader compile failed:\n");
      puts(szError);
      assert(0);
    }
  }

  Misc::InitKeymaps();

#ifdef SCI_LEXER
  Scintilla_LinkLexers();
#endif
  Scintilla::Surface * surface = Scintilla::Surface::Allocate( SC_TECHNOLOGY_DEFAULT );
  surface->Init( NULL );

  int nMargin = 20;

  bool bTexPreviewVisible = false;

  editorOptions.rect = Scintilla::PRectangle( nMargin, nMargin, settings.nWidth - nMargin - nTexPreviewWidth - nMargin, settings.nHeight - nMargin * 2 - nDebugOutputHeight );
  ShaderEditor mShaderEditor( surface );
  mShaderEditor.Initialise( editorOptions );
  mShaderEditor.SetText( szShader );

  editorOptions.rect = Scintilla::PRectangle( nMargin, settings.nHeight - nMargin - nDebugOutputHeight, settings.nWidth - nMargin - nTexPreviewWidth - nMargin, settings.nHeight - nMargin );
  ShaderEditor mDebugOutput( surface );
  mDebugOutput.Initialise( editorOptions );
  mDebugOutput.SetText( "" );
  mDebugOutput.SetReadOnly(true);

  static float fftData[FFT_SIZE];
  memset(fftData, 0, sizeof(float) * FFT_SIZE);
  static float fftDataSmoothed[FFT_SIZE];
  memset(fftDataSmoothed, 0, sizeof(float) * FFT_SIZE);


  static float fftDataSlightlySmoothed[FFT_SIZE];
  memset(fftDataSlightlySmoothed, 0, sizeof(float) * FFT_SIZE);
  static float fftDataIntegrated[FFT_SIZE];
  memset(fftDataIntegrated, 0, sizeof(float) * FFT_SIZE);

  bool bGrabberFollowCaret = true;
  bool bShowGui = true;
  Timer::Start();
  bool shaderFirstNetworkSend = true;
  float shaderTimeOffset = 0;
  float fNextTick = 0.1f;
  float fLastTimeMS = Timer::GetTime();
  float oldtime = Timer::GetTime() / 1000.0;
  while (!Renderer::WantsToQuit())
  {
    
    bool newShader = false;
    bool needEditorUpdate = false;
    float time = Timer::GetTime() / 1000.0; // seconds
    float deltaTime = (time - oldtime);
    Renderer::StartFrame();

    // Networking
    Network::Tick(time);
    
    if (Network::IsShaderNeedUpdate())
    {
      mShaderEditor.GetText(szShader, 65535);
	    Network::ShaderMessage NewMessage;
	    NewMessage.Code = std::string(szShader);
	    NewMessage.NeedRecompile = shaderFirstNetworkSend; // only force recompile on the first send
      shaderFirstNetworkSend = false;
	    NewMessage.CaretPosition = mShaderEditor.WndProc(SCI_GETCURRENTPOS, 0, 0);
	    NewMessage.AnchorPosition = mShaderEditor.WndProc(SCI_GETANCHOR, 0, 0);
      int TopLine = mShaderEditor.WndProc(SCI_GETFIRSTVISIBLELINE, 0, 0);
      NewMessage.FirstVisibleLine = mShaderEditor.WndProc(SCI_DOCLINEFROMVISIBLE, TopLine, 0);
      // midi network parameter update
      if(Network::CanSendMidiControls()) {
        for (std::map<int, std::string>::iterator it = midiRoutes.begin(); it != midiRoutes.end(); it++)
        {
          networkShaderParameters << it->second << MIDI::GetCCValue(it->first);
        }
      }
      Network::SendShader(NewMessage, shaderTimeOffset, networkShaderParameters);
	  }
    
    for(int i=0; i<Renderer::mouseEventBufferCount; i++)
    {
      if (bShowGui)
      {
		    bool IsGrabber = Network::IsConnected() && Network::IsGrabber();
        switch (Renderer::mouseEventBuffer[i].eventType)
        {
          case Renderer::MOUSEEVENTTYPE_MOVE:
            mShaderEditor.ButtonMovePublic( Scintilla::Point( Renderer::mouseEventBuffer[i].x, Renderer::mouseEventBuffer[i].y ) );
            break;
          case Renderer::MOUSEEVENTTYPE_DOWN:
            if(!IsGrabber) mShaderEditor.ButtonDown( Scintilla::Point( Renderer::mouseEventBuffer[i].x, Renderer::mouseEventBuffer[i].y ), time * 1000, false, false, false );
            break;
          case Renderer::MOUSEEVENTTYPE_UP:
            if(!IsGrabber) mShaderEditor.ButtonUp( Scintilla::Point( Renderer::mouseEventBuffer[i].x, Renderer::mouseEventBuffer[i].y ), time * 1000, false );
            break;
          case Renderer::MOUSEEVENTTYPE_SCROLL:
            if(!IsGrabber || !bGrabberFollowCaret) mShaderEditor.WndProc( SCI_LINESCROLL, (int)(-Renderer::mouseEventBuffer[i].x * fScrollXFactor), (int)(-Renderer::mouseEventBuffer[i].y * fScrollYFactor));
            break;
        }
      }
    }
    Renderer::mouseEventBufferCount = 0;

    if (Network::HasNewShader()) {
		  Network::ShaderMessage NewMessage;
		  if(Network::GetNewShader(NewMessage, networkParamCache)) {

			  int PreviousTopLine = mShaderEditor.WndProc(SCI_GETFIRSTVISIBLELINE, 0, 0);
        int PreviousTopDocLine = mShaderEditor.WndProc(SCI_DOCLINEFROMVISIBLE, PreviousTopLine, 0);
        //int PreviousTopLineWrapCount = mShaderEditor.WndProc(SCI_WRAPCOUNT, PreviousTopDocLine, 0) - 1;
        //if(PreviousTopLineWrapCount < 0) PreviousTopLineWrapCount = 0;
        //int PreviousTopLineTotal = PreviousTopDocLine + PreviousTopLineWrapCount;
        int PreviousTopLineTotal = PreviousTopDocLine;

			  mShaderEditor.SetText(NewMessage.Code.c_str());
			  mShaderEditor.WndProc(SCI_SETCURRENTPOS, NewMessage.CaretPosition, 0);
			  mShaderEditor.WndProc(SCI_SETANCHOR, NewMessage.AnchorPosition, 0);
        mShaderEditor.WndProc(SCI_SETFIRSTVISIBLELINE, PreviousTopLineTotal, 0);

        if(bGrabberFollowCaret) {
          //mShaderEditor.WndProc(SCI_SETFIRSTVISIBLELINE, NewMessage.FirstVisibleLine, 0);
          mShaderEditor.WndProc(SCI_SCROLLCARET, 0, 0);
        }
      
        /*
        const char* newCode = NewMessage.Code.c_str();
        mShaderEditor.WndProc(SCI_SETTARGETSTART, 0, 0);
        mShaderEditor.WndProc(SCI_SETTARGETEND, strlen(szShader), 0);
        mShaderEditor.WndProc(SCI_REPLACETARGET, strlen(newCode), (sptr_t)newCode);
        */
        //mShaderEditor.WndProc(SCI_SETCURRENTPOS, NewMessage.CaretPosition, 0);
			  //mShaderEditor.WndProc(SCI_SETANCHOR, NewMessage.AnchorPosition, 0);
			

			  if(NewMessage.NeedRecompile) {
				  mShaderEditor.GetText(szShader, 65535);
				  if (Renderer::ReloadShader(szShader, (int)strlen(szShader), szError, 4096))
				  {
					  // Shader compilation successful; we set a flag to save if the frame render was successful
					  // (If there is a driver crash, don't save.)
					  newShader = true;
				  }
				  else
				  {
					  mDebugOutput.SetText(szError);
				  }
			  }

        Network::AdjustShaderTimeOffset(time, shaderTimeOffset);
		  }
    }

    for(int i=0; i<Renderer::keyEventBufferCount; i++)
    {
      if (Renderer::keyEventBuffer[i].scanCode == 283) // F2
      {
         bTexPreviewVisible = !bTexPreviewVisible;
         needEditorUpdate = true;
      }
      else if (Renderer::keyEventBuffer[i].scanCode == 284) // F3
      {
        bGrabberFollowCaret = !bGrabberFollowCaret;
      }
      else if (Renderer::keyEventBuffer[i].scanCode == 285) // F4
      {
        // adjust offset so that time restarts from 0
        shaderTimeOffset = -time;
      }
      else if (Renderer::keyEventBuffer[i].scanCode == 286 || (Renderer::keyEventBuffer[i].ctrl && Renderer::keyEventBuffer[i].scanCode == 'r')) // F5
      {
        mShaderEditor.GetText(szShader,65535);

        if(Network::IsConnected()) {
		      Network::ShaderMessage NewMessage;
		      NewMessage.Code = std::string(szShader);
		      NewMessage.NeedRecompile = true;
		      NewMessage.CaretPosition = mShaderEditor.WndProc(SCI_GETCURRENTPOS, 0, 0);
		      NewMessage.AnchorPosition = mShaderEditor.WndProc(SCI_GETANCHOR, 0, 0);
          int TopLine = mShaderEditor.WndProc(SCI_GETFIRSTVISIBLELINE, 0, 0);
          NewMessage.FirstVisibleLine = mShaderEditor.WndProc(SCI_DOCLINEFROMVISIBLE, TopLine, 0);
          // midi network parameter update
          if (Network::CanSendMidiControls()) {
            for (std::map<int, std::string>::iterator it = midiRoutes.begin(); it != midiRoutes.end(); it++)
            {
              networkShaderParameters << it->second << MIDI::GetCCValue(it->first);
            }
          }
		      Network::SendShader(NewMessage, shaderTimeOffset, networkShaderParameters);
        }

        if (Renderer::ReloadShader( szShader, (int)strlen(szShader), szError, 4096 ))
        {
          // Shader compilation successful; we set a flag to save if the frame render was successful
          // (If there is a driver crash, don't save.)
          newShader = true;
        }
        else
        {
          mDebugOutput.SetText( szError );
        }
      }
      else if (Renderer::keyEventBuffer[i].scanCode == 292 || (Renderer::keyEventBuffer[i].ctrl && Renderer::keyEventBuffer[i].scanCode == 'f')) // F11 or Ctrl/Cmd-f  
      {
        bShowGui = !bShowGui;
      }
      else if (bShowGui)
      {
        if (!Network::IsConnected() || !Network::IsGrabber())
        {
          bool consumed = false;
          if (Renderer::keyEventBuffer[i].scanCode)
          {
            mShaderEditor.KeyDown(
              iswalpha(Renderer::keyEventBuffer[i].scanCode) ? towupper(Renderer::keyEventBuffer[i].scanCode) : Renderer::keyEventBuffer[i].scanCode,
              Renderer::keyEventBuffer[i].shift,
              Renderer::keyEventBuffer[i].ctrl,
              Renderer::keyEventBuffer[i].alt,
              &consumed);
          }
          if (!consumed && Renderer::keyEventBuffer[i].character)
          {
            char    utf8[5] = { 0,0,0,0,0 };
            wchar_t utf16[2] = { Renderer::keyEventBuffer[i].character, 0 };
            Scintilla::UTF8FromUTF16(utf16, 1, utf8, 4 * sizeof(char));
            mShaderEditor.AddCharUTF(utf8, (unsigned int)strlen(utf8));
          }
        }
      }
    }
    Renderer::keyEventBufferCount = 0;

    Renderer::SetShaderConstant( "fGlobalTime", time + shaderTimeOffset);
    Renderer::SetShaderConstant( "v2Resolution", Renderer::nWidth, Renderer::nHeight );

    float fTime = Timer::GetTime();
    float fDeltaTime = (fTime - fLastTimeMS) / 1000.0f;
    Renderer::SetShaderConstant("fFrameTime", fDeltaTime);
    fLastTimeMS = fTime;

    if (Network::CanGrabMidiControls()) {
      for (auto it = networkParamCache.begin(); it != networkParamCache.end(); it++)
      {
        Network::ShaderParamCache& cache = it->second;
        float motion = (cache.goalValue - cache.lastValue) * deltaTime / std::max(0.001f, cache.duration);
        if (motion > 0.0f) {
          cache.currentValue = std::min(cache.currentValue + motion, cache.goalValue);
        } else {
          cache.currentValue = std::max(cache.currentValue + motion, cache.goalValue);
        } 
        Renderer::SetShaderConstant(it->first.c_str(), cache.currentValue);
      }
    } else {
      for (std::map<int, std::string>::iterator it = midiRoutes.begin(); it != midiRoutes.end(); it++)
      {
        Renderer::SetShaderConstant(it->second.c_str(), MIDI::GetCCValue(it->first));
        Renderer::SetShaderConstant((it->second + "t").c_str(), MIDI::GetCCTimer(it->first, fDeltaTime));
        Renderer::SetShaderConstant((it->second + "b").c_str(), MIDI::GetCCBool(it->first, fDeltaTime));
      }
    }

    if (FFT::GetFFT(fftData))
    {
      Renderer::UpdateR32Texture( texFFT, fftData );

      float integratedScale = deltaTime * 60.0f;

      const static float maxIntegralValue = 1024.0f;
      for ( int i = 0; i < FFT_SIZE; i++ )
      {
        fftDataSmoothed[i] = fftDataSmoothed[i] * fFFTSmoothingFactor + (1 - fFFTSmoothingFactor) * fftData[i];

        fftDataSlightlySmoothed[i] = fftDataSlightlySmoothed[i] * fFFTSlightSmoothingFactor + (1 - fFFTSlightSmoothingFactor) * fftData[i];
        fftDataIntegrated[i] = fftDataIntegrated[i] + fftDataSlightlySmoothed[i]* integratedScale;
        if (fftDataIntegrated[i] > maxIntegralValue) {
          fftDataIntegrated[i] -= maxIntegralValue;
        }
      }

      Renderer::UpdateR32Texture( texFFTSmoothed, fftDataSmoothed );
      Renderer::UpdateR32Texture( texFFTIntegrated, fftDataIntegrated );
    }

    Renderer::SetShaderTexture( "texFFT", texFFT );
    Renderer::SetShaderTexture( "texFFTSmoothed", texFFTSmoothed );
    Renderer::SetShaderTexture( "texFFTIntegrated", texFFTIntegrated );
    Renderer::SetShaderTexture( "texPreviousFrame", texPreviousFrame );

    for (std::map<std::string, Renderer::Texture*>::iterator it = textures.begin(); it != textures.end(); it++)
    {
      Renderer::SetShaderTexture( it->first.c_str(), it->second );
    }

    Renderer::RenderFullscreenQuad();

    Renderer::CopyBackbufferToTexture(texPreviousFrame);

    int TexPreviewOffset = bTexPreviewVisible ? nTexPreviewWidth + nMargin : 0;
    if (needEditorUpdate || Renderer::nSizeChanged)
    {
      mShaderEditor.SetPosition(Scintilla::PRectangle(nMargin, nMargin, Renderer::nWidth - nMargin - TexPreviewOffset, Renderer::nHeight - nMargin * 2 - nDebugOutputHeight));
      mDebugOutput.SetPosition(Scintilla::PRectangle(nMargin, Renderer::nHeight - nMargin - nDebugOutputHeight, Renderer::nWidth - nMargin - TexPreviewOffset, Renderer::nHeight - nMargin));
    }
    
    Renderer::StartTextRendering();

    if (bShowGui)
    {
      if (time > fNextTick)
      {
        mShaderEditor.Tick();
        mDebugOutput.Tick();
        fNextTick = time + 0.1;
      }

      mShaderEditor.Paint();
      mDebugOutput.Paint();

      Renderer::SetTextRenderingViewport(Scintilla::PRectangle(0, 0, Renderer::nWidth, Renderer::nHeight));

      if (bTexPreviewVisible)
      {
        int y1 = nMargin;
        int x1 = Renderer::nWidth - nMargin - nTexPreviewWidth;
        int x2 = Renderer::nWidth - nMargin;
        for (std::map<std::string, Renderer::Texture*>::iterator it = textures.begin(); it != textures.end(); it++)
        {
          int y2 = y1 + nTexPreviewWidth * (it->second->height / (float)it->second->width);
          Renderer::BindTexture(it->second);
          Renderer::RenderQuad(
            Renderer::Vertex(x1, y1, 0xccFFFFFF, 0.0, 0.0),
            Renderer::Vertex(x2, y1, 0xccFFFFFF, 1.0, 0.0),
            Renderer::Vertex(x2, y2, 0xccFFFFFF, 1.0, 1.0),
            Renderer::Vertex(x1, y2, 0xccFFFFFF, 0.0, 1.0)
          );
          surface->DrawTextNoClip(Scintilla::PRectangle(x1, y1, x2, y2), *mShaderEditor.GetTextFont(), y2 - 5.0, it->first.c_str(), (int)it->first.length(), 0xffFFFFFF, 0x00000000);
          y1 = y2 + nMargin;
        }
      }

      /*
      char szLayout[255];
      Misc::GetKeymapName(szLayout);
      std::string sHelp = "F2 - toggle texture preview   F5 or Ctrl-R - recompile shader   F11 - hide GUI   Current keymap: ";
      sHelp += szLayout;
      */

      char frame_ms[10];
      snprintf(frame_ms, sizeof(frame_ms), "%.2f", deltaTime * 1000.0f);
      char frame_fps[10];
      snprintf(frame_fps, sizeof(frame_fps), "%.2f", 1.0f / deltaTime);
      std::string sHelp = "F2 - see textures   F5 or Ctrl-R - recompile   F11 - hide GUI";
      /*
      sHelp += " (";
      sHelp += frame_ms;
      sHelp += "ms ";
      sHelp += frame_fps;
      sHelp += "fps)";
      */
      surface->DrawTextNoClip(Scintilla::PRectangle(20, Renderer::nHeight - 20, 100, Renderer::nHeight), *mShaderEditor.GetTextFont(), Renderer::nHeight - 5.0, sHelp.c_str(), (int)sHelp.length(), 0x80FFFFFF, 0x00000000);
    }

    if (bShowGui || bAlwaysDisplayCoderName)
    {
      if(Network::IsNetworkEnabled()) {
        
        if (!bShowGui) {
          // when editor is not shown, we need to update rendering viewport manually
          Renderer::SetTextRenderingViewport(Scintilla::PRectangle(0, 0, Renderer::nWidth, Renderer::nHeight));
        }

        std::string Status = Network::GetNickName();
        /*
        if(Network::IsConnected()) {
          if(Network::IsLive()) {
            Status += " - Live";
          } else {
            Status += " - Offline";
          }
        } else {
          Status += " - Not Connected";
        }
        */
        
        Scintilla::Font* CoderFont = mShaderEditor.GetCoderTextFont();
        int CoderFontSize = editorOptions.nCoderNameFontSize;

        Scintilla::XYPOSITION WidthText = surface->WidthText(*CoderFont, Status.c_str(), (int)Status.length());
        Scintilla::XYPOSITION RightPosition = Renderer::nWidth - CoderFontSize*0.35 - TexPreviewOffset;
        Scintilla::XYPOSITION LeftPosition = RightPosition - WidthText - CoderFontSize * 0.25;
        Scintilla::XYPOSITION BaseY = Renderer::nHeight - CoderFontSize * 1.1;

        surface->RectangleDraw(Scintilla::PRectangle(LeftPosition, BaseY, RightPosition + CoderFontSize * 0.25, BaseY + CoderFontSize), 0x00000000, 0x80000000);
        surface->DrawTextNoClip(Scintilla::PRectangle(RightPosition - WidthText, BaseY, RightPosition, BaseY + CoderFontSize), *CoderFont, BaseY + CoderFontSize*0.75, Status.c_str(), (int)Status.length(), editorOptions.theme.codername, 0xFFFFFFFF);
        if (!Network::IsLive()) // in case of connection issues
        {
          surface->RectangleDraw(Scintilla::PRectangle(LeftPosition - 20, BaseY, LeftPosition, BaseY + CoderFontSize), 0x00000000, 0x808080FF);
        } else {
          // avoid a strange triangular glitch on coder name when it's the last thing drawn
          surface->PenColour(0);
          surface->MoveTo(0, 0);
          surface->LineTo(0, 0);
        } 
      }
    }

    Renderer::EndTextRendering();

    if (Renderer::nSizeChanged)
    {
      Renderer::ReleaseTexture(texPreviousFrame);
      texPreviousFrame = Renderer::CreateRGBA8Texture();
      Capture::CaptureResize(Renderer::nWidth, Renderer::nHeight);
    }
    Renderer::nSizeChanged = false;

    Renderer::EndFrame();

    Capture::CaptureFrame();

    if (newShader)
    {
      // Frame render successful, save shader
      FILE * f = fopen(shaderFileName.c_str(),"wb");
      if (f)
      {
        fwrite( szShader, strlen(szShader), 1, f );
        fclose(f);
        mDebugOutput.SetText( "" );
      }
      else
      {
        mDebugOutput.SetText( "Unable to save shader! Your work will be lost when you quit!" );
      }
    }
    oldtime = time;
  }


  delete surface;

  MIDI::Close();
  FFT::Close();

  Network::Release();

  Renderer::ReleaseTexture(texPreviousFrame);
  Renderer::ReleaseTexture( texFFT );
  Renderer::ReleaseTexture( texFFTSmoothed );
  for (std::map<std::string, Renderer::Texture*>::iterator it = textures.begin(); it != textures.end(); it++)
  {
    Renderer::ReleaseTexture( it->second );
  }

  Renderer::Close();

  if ( !sPostExitCmd.empty() )
  {
    Misc::ExecuteCommand( sPostExitCmd.c_str(), shaderFileName.c_str() );
  }

  Misc::PlatformShutdown();

  return 0;
}
