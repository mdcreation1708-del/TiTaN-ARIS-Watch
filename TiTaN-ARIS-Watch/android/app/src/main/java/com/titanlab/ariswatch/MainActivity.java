package com.titanlab.ariswatch;

import android.Manifest;
import android.app.Activity;
import android.app.NotificationManager;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanResult;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.speech.RecognizerIntent;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Locale;
import java.util.UUID;

public class MainActivity extends Activity {
    static final UUID SERVICE = UUID.fromString("9f8c0001-7b5a-4d2e-9a11-3c6a7b8c0001");
    static final UUID COMMAND = UUID.fromString("9f8c0002-7b5a-4d2e-9a11-3c6a7b8c0001");
    static final UUID EVENT = UUID.fromString("9f8c0003-7b5a-4d2e-9a11-3c6a7b8c0001");
    static final UUID CCCD = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");
    static MainActivity instance;
    TextView status, log;
    BluetoothAdapter adapter;
    BluetoothGatt gatt;
    BluetoothGattCharacteristic commandChar, eventChar;
    boolean scanning = false;
    final int REQ_SPEECH = 41;

    @Override public void onCreate(Bundle b) {
        super.onCreate(b); instance = this;
        BluetoothManager bm = (BluetoothManager)getSystemService(BLUETOOTH_SERVICE); adapter = bm.getAdapter();
        LinearLayout root = new LinearLayout(this); root.setOrientation(LinearLayout.VERTICAL); root.setPadding(22,22,22,22); root.setBackgroundColor(0xff050a18);
        status = label("TiTaN ARIS Companion — Disconnected", 18, 0xff00d9ff); root.addView(status);
        addButton(root,"Scan for TiTaN ARIS Watch",v->scan());
        addButton(root,"Connect to first matching watch",v->scan());
        addButton(root,"Send test / status",v->send("STATUS"));
        addButton(root,"ARIS voice input (phone mic)",v->startVoice());
        addButton(root,"Open notification access settings",v->startActivity(new Intent("android.settings.ACTION_NOTIFICATION_LISTENER_SETTINGS")));
        addButton(root,"Clear watch notification",v->send("CLEAR"));
        log=label("Ready. Grant Bluetooth permissions and notification access.\nVoice currently transcribes and forwards text; an AI backend is not included in this starter.",14,0xffeeeeee);
        ScrollView sv=new ScrollView(this);sv.addView(log);root.addView(sv,new LinearLayout.LayoutParams(-1,0,1));setContentView(root);
        requestBluetoothPermissions();
    }
    TextView label(String s,int size,int color){TextView t=new TextView(this);t.setText(s);t.setTextSize(size);t.setTextColor(color);t.setPadding(4,10,4,10);return t;}
    interface Click {void run(View v);} void addButton(LinearLayout root,String text,Click click){Button b=new Button(this);b.setText(text);b.setOnClickListener(click::run);root.addView(b);}
    void say(String s){runOnUiThread(()->{log.append("\n"+s);});}
    void requestBluetoothPermissions(){if(Build.VERSION.SDK_INT>=31){ArrayList<String> p=new ArrayList<>();if(checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN)!=PackageManager.PERMISSION_GRANTED)p.add(Manifest.permission.BLUETOOTH_SCAN);if(checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT)!=PackageManager.PERMISSION_GRANTED)p.add(Manifest.permission.BLUETOOTH_CONNECT);if(!p.isEmpty())requestPermissions(p.toArray(new String[0]),7);}}
    void scan(){if(adapter==null||!adapter.isEnabled()){say("Enable Bluetooth on the phone first.");return;}if(Build.VERSION.SDK_INT>=31&&(checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN)!=PackageManager.PERMISSION_GRANTED||checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT)!=PackageManager.PERMISSION_GRANTED)){requestBluetoothPermissions();return;}
        BluetoothLeScanner scanner=adapter.getBluetoothLeScanner();if(scanner==null){say("BLE scanner unavailable.");return;}say("Scanning for TiTaN ARIS Watch...");scanning=true;
        scanner.startScan(new ScanCallback(){@Override public void onScanResult(int type,ScanResult r){BluetoothDevice d=r.getDevice();String name="";try{name=d.getName();}catch(SecurityException ignored){} if("TiTaN ARIS Watch".equals(name)){scanner.stopScan(this);scanning=false;say("Found watch; connecting...");gatt=d.connectGatt(MainActivity.this,false,gattCallback);}}
        @Override public void onScanFailed(int e){scanning=false;say("BLE scan failed: "+e);}});
        status.setText("Scanning…");
    }
    final BluetoothGattCallback gattCallback=new BluetoothGattCallback(){
      @Override public void onConnectionStateChange(BluetoothGatt g,int s,int state){if(state==BluetoothGatt.STATE_CONNECTED){gatt=g;say("Connected. Discovering services...");g.discoverServices();}else if(state==BluetoothGatt.STATE_DISCONNECTED){say("Watch disconnected.");status.setText("Disconnected");commandChar=null;eventChar=null;}}
      @Override public void onServicesDiscovered(BluetoothGatt g,int s){BluetoothGattService svc=g.getService(SERVICE);if(svc==null){say("Watch service UUID not found. Check firmware version.");return;}commandChar=svc.getCharacteristic(COMMAND);eventChar=svc.getCharacteristic(EVENT);if(eventChar!=null){g.setCharacteristicNotification(eventChar,true);BluetoothGattDescriptor d=eventChar.getDescriptor(CCCD);if(d!=null){d.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);g.writeDescriptor(d);}}status.setText("Connected to TiTaN ARIS Watch");say("BLE ready. Sending PING.");send("PING");}
      @Override public void onCharacteristicChanged(BluetoothGatt g,BluetoothGattCharacteristic c){String v=new String(c.getValue(),StandardCharsets.UTF_8);say("WATCH: "+v);}
      @Override public void onCharacteristicRead(BluetoothGatt g,BluetoothGattCharacteristic c,int s){say("WATCH: "+new String(c.getValue(),StandardCharsets.UTF_8));}
    };
    void send(String message){if(gatt==null||commandChar==null){say("Not connected to watch.");return;}if(Build.VERSION.SDK_INT>=31&&checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT)!=PackageManager.PERMISSION_GRANTED){requestBluetoothPermissions();return;}commandChar.setWriteType(BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);commandChar.setValue(message.getBytes(StandardCharsets.UTF_8));boolean ok=gatt.writeCharacteristic(commandChar);say((ok?"Sent: ":"Could not queue: ")+message);}
    void startVoice(){if(checkSelfPermission(Manifest.permission.RECORD_AUDIO)!=PackageManager.PERMISSION_GRANTED){requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO},8);return;}Intent i=new Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH);i.putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL,RecognizerIntent.LANGUAGE_MODEL_FREE_FORM);i.putExtra(RecognizerIntent.EXTRA_LANGUAGE,Locale.getDefault());i.putExtra(RecognizerIntent.EXTRA_PROMPT,"Speak to ARIS");try{startActivityForResult(i,REQ_SPEECH);}catch(Exception e){say("Speech recognition unavailable on this phone.");}}
    @Override protected void onActivityResult(int request,int result,Intent data){super.onActivityResult(request,result,data);if(request==REQ_SPEECH&&result==RESULT_OK&&data!=null){ArrayList<String> results=data.getStringArrayListExtra(RecognizerIntent.EXTRA_RESULTS);if(results!=null&&!results.isEmpty()){String spoken=results.get(0);say("YOU: "+spoken);send("VOICE|"+spoken);say("Text forwarded to watch. AI response service is not configured in this build.");}}}
    public static void forwardNotification(String title,String body){MainActivity a=instance;if(a==null)return;String t=clean(title,70),b=clean(body,180);a.runOnUiThread(()->a.send("NOTE|"+t+"|"+b));}
    static String clean(String s,int n){if(s==null)return "";return s.replace('|',' ').replace('\n',' ').replace('\r',' ').substring(0,Math.min(s.length(),n));}
    @Override protected void onDestroy(){if(gatt!=null){gatt.disconnect();gatt.close();gatt=null;}instance=null;super.onDestroy();}
}
