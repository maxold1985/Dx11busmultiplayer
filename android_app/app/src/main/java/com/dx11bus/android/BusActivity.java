package com.dx11bus.android;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.graphics.Color;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.io.BufferedInputStream;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipEntry;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public final class BusActivity extends Activity {
    static { System.loadLibrary("bus_android_app"); }

    private static final int PICK_MODEL = 101;
    private static final int PICK_SCRIPT = 102;
    private static final int PICK_FOLDER = 103;
    private static final int PICK_ZIP = 104;
    private static final int MAX_FILES = 12000;
    private static final long MAX_TOTAL_BYTES = 1536L * 1024L * 1024L;

    public static native void nativeInitGL();
    public static native void nativeResize(int width, int height);
    public static native void nativeDraw();
    public static native boolean nativeConnect(String ipv4);
    public static native void nativeDisconnect();
    public static native boolean nativeLoadModel(String file);
    public static native boolean nativeLoadScript(String file);
    public static native void nativeControls(float throttle, float steer, float brake, float clutch);
    public static native void nativeFlag(int flag);
    public static native void nativeCamera(float dx, float dy, float zoom);
    public static native void nativeCockpit();
    public static native void nativeGlassDiagnostic();
    public static native void nativeGlassFaces();
    public static native String nativeStatus();
    public static native void nativeSoundEvent(String event);
    public static native void nativePause();
    public static native void nativeResume();
    public static native void nativeZoom(float amount);

    private GLSurfaceView surface;
    private TextView status;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private boolean forward, reverse, left, right, brake, clutch;
    private float padThrottle, padSteering, padBrake;
    private float touchX, touchY, pinchDistance;
    private boolean dragging;
    private final Runnable statusTicker=new Runnable() {
        @Override public void run() {
            if(surface!=null)surface.queueEvent(() -> {
                final String text=nativeStatus();
                runOnUiThread(() -> status.setText(text));
            });
            handler.postDelayed(this,500);
        }
    };
    private final Runnable frameTicker=new Runnable() {
        @Override public void run() {
            if(surface!=null)surface.requestRender();
            handler.postDelayed(this,33);
        }
    };
    private String importedModel = "";
    private String importedScript = "";
    private volatile File lastImportedFolder = null;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().getDecorView().setSystemUiVisibility(
            View.SYSTEM_UI_FLAG_FULLSCREEN |
            View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);

        FrameLayout root = new FrameLayout(this);
        surface = new GLSurfaceView(this);
        surface.setEGLContextClientVersion(3);
        surface.setEGLConfigChooser(8, 8, 8, 8, 24, 8);
        surface.setPreserveEGLContextOnPause(true);
        surface.setRenderer(new GLSurfaceView.Renderer() {
            @Override public void onSurfaceCreated(GL10 gl, EGLConfig config) {
                nativeInitGL();
            }
            @Override public void onSurfaceChanged(GL10 gl, int w, int h) {
                nativeResize(w, h);
            }
            @Override public void onDrawFrame(GL10 gl) {
                nativeDraw();
            }
        });
        // 30 FPS limits CPU/GPU use and battery drain on mobile devices.
        surface.setRenderMode(GLSurfaceView.RENDERMODE_WHEN_DIRTY);
        surface.setOnTouchListener((v, e) -> {
            switch(e.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    touchX=e.getX();touchY=e.getY();dragging=true;return true;
                case MotionEvent.ACTION_POINTER_DOWN:
                    if(e.getPointerCount()>1) {
                        pinchDistance=(float)Math.hypot(e.getX(1)-e.getX(0),
                                                           e.getY(1)-e.getY(0));
                        dragging=false;
                    }
                    return true;
                case MotionEvent.ACTION_MOVE:
                    if(e.getPointerCount()>1) {
                        float next=(float)Math.hypot(e.getX(1)-e.getX(0),
                                                    e.getY(1)-e.getY(0));
                        final float zoom=(pinchDistance-next)*0.025f;
                        pinchDistance=next;
                        surface.queueEvent(() -> nativeZoom(zoom));
                    } else if(dragging) {
                        final float dx=(e.getX()-touchX)*0.008f;
                        final float dy=(e.getY()-touchY)*0.005f;
                        touchX=e.getX();touchY=e.getY();
                        surface.queueEvent(() -> nativeCamera(dx,dy,0));
                    }
                    return true;
                case MotionEvent.ACTION_POINTER_UP:
                    dragging=false;return true;
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_CANCEL:
                    dragging=false;return true;
                default:return true;
            }
        });
        root.addView(surface);

        LinearLayout top = new LinearLayout(this);
        top.setOrientation(LinearLayout.VERTICAL);
        top.setBackgroundColor(0x70000000);
        status = new TextView(this);
        status.setTextColor(Color.WHITE);
        status.setTextSize(12);
        status.setMaxLines(4);
        status.setText("Android OpenGL ES 3.0 | Aguardando...");
        status.setPadding(dp(8),dp(3),dp(8),dp(3));
        top.addView(status);
        HorizontalScrollView tools = new HorizontalScrollView(this);
        tools.setHorizontalScrollBarEnabled(false);
        LinearLayout actions = new LinearLayout(this);
        addAction(actions,"MODELO",() -> openFile(PICK_MODEL));
        addAction(actions,"SCRIPT",() -> openFile(PICK_SCRIPT));
        addAction(actions,"PASTA OMSI",this::openFolder);
        addAction(actions,"ZIP OMSI",() -> openFile(PICK_ZIP));
        addAction(actions,"MODELOS MOD",() -> chooseFromImportedFolder(false));
        addAction(actions,"SCRIPTS MOD",() -> chooseFromImportedFolder(true));
        addAction(actions,"CONECTAR",this::askServer);
        addAction(actions,"DESCONECTAR",() -> surface.queueEvent(BusActivity::nativeDisconnect));
        addAction(actions,"CAMERA",() -> surface.queueEvent(BusActivity::nativeCockpit));
        addAction(actions,"ZOOM +",() -> surface.queueEvent(() -> nativeZoom(-2.0f)));
        addAction(actions,"ZOOM -",() -> surface.queueEvent(() -> nativeZoom(2.0f)));
        addAction(actions,"VIDRO COR",() -> surface.queueEvent(BusActivity::nativeGlassDiagnostic));
        addAction(actions,"VIDRO FACES",() -> surface.queueEvent(BusActivity::nativeGlassFaces));
        addAction(actions,"RESET",() -> surface.queueEvent(() -> nativeFlag(16)));
        tools.addView(actions);
        top.addView(tools);
        FrameLayout.LayoutParams topParams = new FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.WRAP_CONTENT,
            Gravity.TOP);
        root.addView(top,topParams);

        LinearLayout bottom = new LinearLayout(this);
        bottom.setOrientation(LinearLayout.VERTICAL);
        bottom.setBackgroundColor(0x70000000);
        LinearLayout driving = new LinearLayout(this);
        driving.setGravity(Gravity.CENTER);
        addHold(driving,"ACELERAR",1);
        addHold(driving,"RE",2);
        addHold(driving,"ESQUERDA",3);
        addHold(driving,"DIREITA",4);
        addHold(driving,"FREIO",5);
        addHold(driving,"EMBREAGEM",6);
        bottom.addView(driving);
        HorizontalScrollView otherScroll=new HorizontalScrollView(this);
        LinearLayout other=new LinearLayout(this);
        addAction(other,"PORTA",() -> surface.queueEvent(() -> nativeFlag(1)));
        addAction(other,"MARCHA +",() -> surface.queueEvent(() -> nativeFlag(2)));
        addAction(other,"MARCHA -",() -> surface.queueEvent(() -> nativeFlag(4)));
        addAction(other,"AUTO",() -> surface.queueEvent(() -> nativeFlag(8)));
        addAction(other,"DIRECAO OMSI",() -> surface.queueEvent(() -> nativeFlag(32)));
        addAction(other,"BUZINA",() -> surface.queueEvent(() -> nativeSoundEvent("horn")));
        addAction(other,"PARADA",() -> surface.queueEvent(() -> nativeSoundEvent("stopRequest")));
        addAction(other,"SETA",() -> surface.queueEvent(() -> nativeSoundEvent("blinkers")));
        addAction(other,"FREIO MAO",() -> surface.queueEvent(() -> nativeSoundEvent("parkingBrakeOn")));
        otherScroll.addView(other);
        bottom.addView(otherScroll);
        FrameLayout.LayoutParams bottomParams=new FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.MATCH_PARENT,FrameLayout.LayoutParams.WRAP_CONTENT,
            Gravity.BOTTOM);
        root.addView(bottom,bottomParams);
        setContentView(root);

        handler.postDelayed(statusTicker,500);
    }

    private int dp(int n) {
        return (int)(n*getResources().getDisplayMetrics().density+0.5f);
    }
    private void addAction(LinearLayout parent,String title,Runnable action) {
        Button button=new Button(this);
        button.setText(title);
        button.setTextSize(10);
        button.setMinHeight(0);
        button.setMinWidth(0);
        button.setPadding(dp(5),0,dp(5),0);
        button.setOnClickListener(v -> action.run());
        parent.addView(button,new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.WRAP_CONTENT,dp(45)));
    }
    private void addHold(LinearLayout parent,String name,int key) {
        Button button=new Button(this);
        button.setText(name);
        button.setTextSize(10);
        button.setMinWidth(0);
        button.setMinHeight(0);
        button.setPadding(dp(2),0,dp(2),0);
        button.setOnTouchListener((v,e)->{
            int action=e.getActionMasked();
            if(action==MotionEvent.ACTION_DOWN || action==MotionEvent.ACTION_UP ||
               action==MotionEvent.ACTION_CANCEL) {
                setKey(key,action==MotionEvent.ACTION_DOWN);
                return true;
            }
            return true;
        });
        parent.addView(button,new LinearLayout.LayoutParams(0,dp(48),1));
    }
    private void setKey(int key,boolean held) {
        switch(key) {
            case 1:forward=held;break;
            case 2:reverse=held;break;
            case 3:left=held;break;
            case 4:right=held;break;
            case 5:brake=held;break;
            case 6:clutch=held;break;
        }
        sendControls();
    }
    private void sendControls() {
        float t=(forward?1:0)-(reverse?1:0);
        float s=(right?1:0)-(left?1:0);
        if(t==0)t=padThrottle;
        if(s==0)s=padSteering;
        float b=Math.max(brake?1.0f:0.0f,padBrake);
        float c=clutch?1.0f:0.0f;
        surface.queueEvent(() -> nativeControls(t,s,b,c));
    }
    private void openFile(int request) {
        Intent intent=new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent,request);
    }
    private void openFolder() {
        Intent intent=new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        startActivityForResult(intent,PICK_FOLDER);
    }
    private void askServer() {
        EditText ip=new EditText(this);
        ip.setSingleLine(true);
        ip.setText(getPreferences(MODE_PRIVATE).getString("lastServer","192.168.1.10"));
        ip.setSelectAllOnFocus(true);
        new AlertDialog.Builder(this).setTitle("Servidor BUS4 (IPv4)")
            .setView(ip)
            .setPositiveButton("Conectar",(dialog,which)->{
                String address=ip.getText().toString().trim();
                getPreferences(MODE_PRIVATE).edit().putString("lastServer",address).apply();
                surface.queueEvent(() -> {
                    boolean ok=nativeConnect(address);
                    runOnUiThread(() -> toast(ok?"Tentando conexao UDP...":"Endereco IPv4 invalido"));
                });
            })
            .setNegativeButton("Cancelar",null).show();
    }
    private void toast(String message) {
        Toast.makeText(this,message,Toast.LENGTH_SHORT).show();
    }
    private static float gamepadDeadzone(float x) {
        return Math.abs(x)<0.12f?0.0f:Math.max(-1.0f,Math.min(1.0f,x));
    }
    @Override public boolean onGenericMotionEvent(MotionEvent event) {
        if((event.getSource() & InputDevice.SOURCE_JOYSTICK)==
            InputDevice.SOURCE_JOYSTICK &&
            event.getAction()==MotionEvent.ACTION_MOVE) {
            padThrottle=gamepadDeadzone(-event.getAxisValue(MotionEvent.AXIS_Y));
            padSteering=gamepadDeadzone(event.getAxisValue(MotionEvent.AXIS_X));
            padBrake=Math.max(0.0f,Math.min(1.0f,Math.max(
                event.getAxisValue(MotionEvent.AXIS_LTRIGGER),
                event.getAxisValue(MotionEvent.AXIS_BRAKE))));
            sendControls();
            return true;
        }
        return super.onGenericMotionEvent(event);
    }
    @Override public boolean onKeyDown(int keyCode,KeyEvent event) {
        switch(keyCode) {
            case KeyEvent.KEYCODE_W: setKey(1,true);return true;
            case KeyEvent.KEYCODE_S: setKey(2,true);return true;
            case KeyEvent.KEYCODE_A: setKey(3,true);return true;
            case KeyEvent.KEYCODE_D: setKey(4,true);return true;
            case KeyEvent.KEYCODE_SPACE: setKey(5,true);return true;
            case KeyEvent.KEYCODE_TAB: setKey(6,true);return true;
            case KeyEvent.KEYCODE_E:
                if(event.getRepeatCount()==0)
                    surface.queueEvent(() -> nativeFlag(1));
                return true;
            case KeyEvent.KEYCODE_Q:
                if(event.getRepeatCount()==0)
                    surface.queueEvent(() -> nativeFlag(2));
                return true;
            case KeyEvent.KEYCODE_Z:
                if(event.getRepeatCount()==0)
                    surface.queueEvent(() -> nativeFlag(4));
                return true;
            case KeyEvent.KEYCODE_G:
                if(event.getRepeatCount()==0)
                    surface.queueEvent(() -> nativeFlag(8));
                return true;
            case KeyEvent.KEYCODE_R:
                if(event.getRepeatCount()==0)
                    surface.queueEvent(() -> nativeFlag(16));
                return true;
            case KeyEvent.KEYCODE_BUTTON_A:
                if(event.getRepeatCount()==0)
                    surface.queueEvent(() -> nativeFlag(1));
                return true;
            case KeyEvent.KEYCODE_BUTTON_R1:
                if(event.getRepeatCount()==0)
                    surface.queueEvent(() -> nativeFlag(2));
                return true;
            case KeyEvent.KEYCODE_H:
                if(event.getRepeatCount()==0)
                    surface.queueEvent(() -> nativeSoundEvent("horn"));
                return true;
            case KeyEvent.KEYCODE_B:
                if(event.getRepeatCount()==0)
                    surface.queueEvent(() -> nativeSoundEvent("stopRequest"));
                return true;
            case KeyEvent.KEYCODE_F6:
                if(event.getRepeatCount()==0)
                    surface.queueEvent(BusActivity::nativeGlassDiagnostic);
                return true;
            case KeyEvent.KEYCODE_F7:
                if(event.getRepeatCount()==0)
                    surface.queueEvent(BusActivity::nativeGlassFaces);
                return true;
            case KeyEvent.KEYCODE_F1:
                if(event.getRepeatCount()==0)
                    surface.queueEvent(BusActivity::nativeCockpit);
                return true;
            default:return super.onKeyDown(keyCode,event);
        }
    }
    @Override public boolean onKeyUp(int keyCode,KeyEvent event) {
        switch(keyCode) {
            case KeyEvent.KEYCODE_W:setKey(1,false);return true;
            case KeyEvent.KEYCODE_S:setKey(2,false);return true;
            case KeyEvent.KEYCODE_A:setKey(3,false);return true;
            case KeyEvent.KEYCODE_D:setKey(4,false);return true;
            case KeyEvent.KEYCODE_SPACE:setKey(5,false);return true;
            case KeyEvent.KEYCODE_TAB:setKey(6,false);return true;
            default:return super.onKeyUp(keyCode,event);
        }
    }

    @Override protected void onDestroy() {
        handler.removeCallbacksAndMessages(null);
        super.onDestroy();
    }
    @Override protected void onPause() {
        handler.removeCallbacks(statusTicker);
        forward=reverse=left=right=brake=clutch=false;
        padThrottle=padSteering=padBrake=0.0f;
        handler.removeCallbacks(frameTicker);
        surface.queueEvent(() -> {
            nativeControls(0,0,0,0);
            nativePause();
        });
        surface.onPause();
        super.onPause();
    }
    @Override protected void onResume() {
        super.onResume();
        if(surface!=null) {
            surface.onResume();
            surface.queueEvent(BusActivity::nativeResume);
            handler.removeCallbacks(frameTicker);
            handler.postDelayed(frameTicker,33);
            handler.removeCallbacks(statusTicker);
            handler.postDelayed(statusTicker,500);
        }
    }

    @Override protected void onActivityResult(int request,int result,Intent data) {
        super.onActivityResult(request,result,data);
        if(result!=RESULT_OK || data==null || data.getData()==null)return;
        Uri uri=data.getData();
        if(request==PICK_FOLDER) {
            toast("Importando pasta em segundo plano...");
            new Thread(() -> {
                try {
                    File folder=new File(getFilesDir(),"omsi_"+System.currentTimeMillis());
                    if(!folder.mkdirs())throw new Exception("Nao foi possivel criar pasta");
                    CopyCounter counter=new CopyCounter();
                    String rootId=DocumentsContract.getTreeDocumentId(uri);
                    copyTree(uri,rootId,folder,counter,0);
                    finishImportedFolder(folder,counter.files);
                } catch(Exception error) {
                    runOnUiThread(() -> toast("Falha na importacao: "+error.getMessage()));
                }
            },"OMSI-folder-import").start();
        } else if(request==PICK_ZIP) {
            toast("Extraindo ZIP OMSI...");
            new Thread(() -> {
                try {
                    File folder=new File(getFilesDir(),"omsi_zip_"+System.currentTimeMillis());
                    if(!folder.mkdirs())throw new Exception("Falha criando diretorio ZIP");
                    CopyCounter counter=new CopyCounter();
                    copyZip(uri,folder,counter);
                    finishImportedFolder(folder,counter.files);
                } catch(Exception error) {
                    runOnUiThread(() -> toast("Falha no ZIP: "+error.getMessage()));
                }
            },"OMSI-zip-import").start();
        } else {
            toast("Copiando arquivo...");
            new Thread(() -> {
                try {
                    String name=safeName(displayName(uri));
                    File destDir=new File(getFilesDir(),"selected");
                    if(!destDir.exists() && !destDir.mkdirs())
                        throw new Exception("Falha criando pasta privada");
                    File out=new File(destDir,System.currentTimeMillis()+"_"+name);
                    copyUri(uri,out,new CopyCounter());
                    surface.queueEvent(() -> {
                        boolean ok;
                        if(request==PICK_MODEL) {
                            importedModel=out.getAbsolutePath();
                            ok=nativeLoadModel(importedModel);
                        } else {
                            importedScript=out.getAbsolutePath();
                            ok=nativeLoadScript(importedScript);
                        }
                        runOnUiThread(() -> toast(ok?"Arquivo carregado":
                            "Falha: importe a pasta completa para dependencias OMSI"));
                    });
                } catch(Exception error) {
                    runOnUiThread(() -> toast(error.getMessage()));
                }
            },"OMSI-file-import").start();
        }
    }
    private void finishImportedFolder(File folder,int copiedFiles) {
        lastImportedFolder=folder;
        ImportSelection selection=new ImportSelection();
        findCandidates(folder,selection);
        final String model=selection.model!=null?selection.model.getAbsolutePath():"";
        final String script=selection.script!=null?selection.script.getAbsolutePath():"";
        importedModel=model;
        importedScript=script;
        surface.queueEvent(() -> {
            boolean modelOk=!model.isEmpty() && nativeLoadModel(model);
            boolean scriptOk=!script.isEmpty() && nativeLoadScript(script);
            runOnUiThread(() -> toast("Importado: "+copiedFiles+
                " arquivos | Modelo: "+modelOk+" | Script: "+scriptOk));
        });
    }
    private void copyZip(Uri uri,File directory,CopyCounter counter) throws Exception {
        final String safeRoot=directory.getCanonicalPath()+File.separator;
        int entries=0;
        try(InputStream input=getContentResolver().openInputStream(uri)) {
            if(input==null)throw new Exception("ZIP inacessivel");
            try(ZipInputStream zip=new ZipInputStream(new BufferedInputStream(input))) {
                ZipEntry entry;
                byte[] buffer=new byte[65536];
                while((entry=zip.getNextEntry())!=null) {
                    if(++entries>MAX_FILES*2)
                        throw new Exception("ZIP possui entradas demais");
                    String relative=entry.getName().replace('\\','/');
                    if(relative.length()>1024 || relative.startsWith("/") ||
                       relative.indexOf(':')>=0)
                        throw new Exception("Caminho ZIP invalido");
                    File out=new File(directory,relative);
                    String canonical=out.getCanonicalPath();
                    if(!canonical.startsWith(safeRoot))
                        throw new Exception("ZIP com caminho fora da pasta");
                    if(entry.isDirectory()) {
                        if(!out.isDirectory() && !out.mkdirs())
                            throw new Exception("Falha criando diretorio ZIP");
                    } else {
                        if(++counter.files>MAX_FILES)
                            throw new Exception("ZIP possui arquivos demais");
                        File parent=out.getParentFile();
                        if(parent!=null && !parent.exists() && !parent.mkdirs())
                            throw new Exception("Falha criando pasta do ZIP");
                        long local=0;
                        try(FileOutputStream file=new FileOutputStream(out)) {
                            int length;
                            while((length=zip.read(buffer))!=-1) {
                                local+=length;
                                counter.bytes+=length;
                                if(local>256L*1024L*1024L ||
                                   counter.bytes>MAX_TOTAL_BYTES)
                                    throw new Exception("ZIP excede limite de tamanho");
                                file.write(buffer,0,length);
                            }
                        }
                    }
                    zip.closeEntry();
                }
            }
        }
        if(counter.files==0)throw new Exception("ZIP nao contem arquivos");
    }
    private String displayName(Uri uri) {
        try(Cursor c=getContentResolver().query(uri,new String[]{OpenableColumns.DISPLAY_NAME},
            null,null,null)) {
            if(c!=null && c.moveToFirst())return c.getString(0);
        }
        return "arquivo";
    }
    private static String safeName(String name) {
        if(name==null||name.isEmpty()||name.equals(".")||name.equals(".."))return "arquivo";
        return name.replace('/','_').replace('\\','_').replace(':','_').replace('\0','_');
    }
    private static final class CopyCounter {int files;long bytes;}
    private void copyUri(Uri uri,File target,CopyCounter counter) throws Exception {
        if(++counter.files>MAX_FILES)throw new Exception("Limite de arquivos excedido");
        File parent=target.getParentFile();
        if(parent!=null && !parent.exists() && !parent.mkdirs())
            throw new Exception("Falha criando "+parent.getName());
        try(InputStream input=getContentResolver().openInputStream(uri);
            FileOutputStream output=new FileOutputStream(target)) {
            if(input==null)throw new Exception("Arquivo inacessivel");
            byte[] buffer=new byte[65536];
            int read;
            long localBytes=0;
            while((read=input.read(buffer))!=-1) {
                counter.bytes+=read;
                localBytes+=read;
                if(counter.bytes>MAX_TOTAL_BYTES || localBytes>256L*1024L*1024L)
                    throw new Exception("Importacao excedeu o limite de tamanho");
                output.write(buffer,0,read);
            }
        }
    }
    private void copyTree(Uri treeUri,String documentId,File directory,
                          CopyCounter counter,int depth) throws Exception {
        if(depth>24)throw new Exception("Pastas muito profundas");
        Uri children=DocumentsContract.buildChildDocumentsUriUsingTree(treeUri,documentId);
        String[] projection={
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE};
        try(Cursor c=getContentResolver().query(children,projection,null,null,null)) {
            if(c==null)throw new Exception("Pasta inacessivel");
            while(c.moveToNext()) {
                String id=c.getString(0);
                String name=safeName(c.getString(1));
                String mime=c.getString(2);
                File out=new File(directory,name);
                if(DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                    if(!out.isDirectory() && !out.mkdirs())
                        throw new Exception("Falha criando "+name);
                    copyTree(treeUri,id,out,counter,depth+1);
                } else {
                    Uri file=DocumentsContract.buildDocumentUriUsingTree(treeUri,id);
                    copyUri(file,out,counter);
                }
            }
        }
    }
    private void collectChoices(File root,boolean scripts,List<File> list,int depth) {
        if(depth>20 || list.size()>=180)return;
        File[] files=root.listFiles();
        if(files==null)return;
        java.util.Arrays.sort(files,(a,b)->a.getName().compareToIgnoreCase(b.getName()));
        for(File file:files) {
            if(list.size()>=180)return;
            if(file.isDirectory()) {
                collectChoices(file,scripts,list,depth+1);
                continue;
            }
            final String lower=file.getName().toLowerCase(java.util.Locale.ROOT);
            if(scripts) {
                if(lower.endsWith(".ini") ||
                   lower.equals("motor.txt") ||
                   lower.equals("cambio_a.txt") ||
                   lower.equals("cambio_m.txt"))
                    list.add(file);
            } else {
                if(lower.endsWith(".bus") ||
                   lower.equals("model.cfg") ||
                   lower.endsWith(".3ds") ||
                   lower.endsWith(".o3d") ||
                   lower.endsWith(".glb") ||
                   lower.endsWith(".fbx") ||
                   lower.endsWith(".x"))
                    list.add(file);
            }
        }
    }
    private void chooseFromImportedFolder(boolean scripts) {
        final File root=lastImportedFolder;
        if(root==null || !root.isDirectory()) {
            toast("Importe a PASTA OMSI primeiro");
            return;
        }
        List<File> files=new ArrayList<>();
        collectChoices(root,scripts,files,0);
        if(files.isEmpty()) {
            toast("Nenhum arquivo encontrado na pasta importada");
            return;
        }
        String[] labels=new String[files.size()];
        for(int i=0;i<files.size();++i) {
            String relative=files.get(i).getAbsolutePath().substring(
                root.getAbsolutePath().length()+1);
            labels[i]=relative;
        }
        new AlertDialog.Builder(this)
            .setTitle(scripts?"Escolher script do mod":"Escolher modelo do mod")
            .setItems(labels,(dialog,selected)->{
                File picked=files.get(selected);
                surface.queueEvent(() -> {
                    boolean ok;
                    if(scripts) {
                        importedScript=picked.getAbsolutePath();
                        ok=nativeLoadScript(importedScript);
                    } else {
                        importedModel=picked.getAbsolutePath();
                        ok=nativeLoadModel(importedModel);
                    }
                    runOnUiThread(() -> toast(ok?"Carregado: "+picked.getName():
                        "Nao foi possivel carregar: "+picked.getName()));
                });
            })
            .setNegativeButton("Cancelar",null)
            .show();
    }
    private static final class ImportSelection {
        File model,script;
        int modelPriority=99,scriptPriority=99;
    }
    private void findCandidates(File folder,ImportSelection selection) {
        File[] children=folder.listFiles();
        if(children==null)return;
        for(File file:children) {
            if(file.isDirectory()) {findCandidates(file,selection);continue;}
            String name=file.getName().toLowerCase(java.util.Locale.ROOT);
            int priority=99;
            if(name.endsWith(".bus"))priority=0;
            else if(name.equals("model.cfg"))priority=1;
            else if(name.endsWith(".3ds"))priority=2;
            else if(name.endsWith(".o3d"))priority=3;
            if(priority<selection.modelPriority) {
                selection.model=file;selection.modelPriority=priority;
            }
            if(name.endsWith(".ini")) {
                int p=name.contains("bus")?0:1;
                if(p<selection.scriptPriority) {
                    selection.script=file;selection.scriptPriority=p;
                }
            }
        }
    }
}
