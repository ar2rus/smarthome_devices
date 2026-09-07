#!/usr/bin/env python3
"""Host Java regression: real bridge/SSE clients, mocked Android/HTTP; not an APK build."""
import argparse
from pathlib import Path
import subprocess,tempfile,shutil
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--monitor',type=Path,default=Path.home()/'Documents/GitHub/smarthome_monitor')
p.add_argument('--original',type=Path,default=Path.home()/'Documents/GitHub/smarthome_monitor')
p.add_argument('--javac',type=Path,default=Path.home()/'Library/Java/JavaVirtualMachines/temurin-17.0.20.1/Contents/Home/bin/javac')
p.add_argument('--jackson-dir',type=Path,default=Path.home()/'.gradle/wrapper/dists/gradle-8.0-bin/ca5e32bp14vu59qr306oxotwh/gradle-8.0/lib/plugins')
a=p.parse_args();w=Path(tempfile.mkdtemp(prefix='android-client-regression-'))
files={
'android/content/Context.java':'package android.content; public class Context { public Context getApplicationContext(){return this;} }',
'android/os/SystemClock.java':'package android.os; public class SystemClock { public static long now=0; public static long elapsedRealtime(){return now;} }',
'android/util/Log.java':'package android.util; public class Log { public static int w(String s,String m,Throwable t){return 0;} }',
'com/gargon/smarthome/config/BridgeSettings.java':'package com.gargon.smarthome.config; public class BridgeSettings { public static String getBaseUrl(android.content.Context c){return "http://bridge/";} }',
'okio/ByteString.java':'package okio; public class ByteString { byte[] b; public static ByteString decodeHex(String s){ ByteString r=new ByteString(); r.b=new byte[s.length()/2]; for(int i=0;i<r.b.length;i++)r.b[i]=(byte)Integer.parseInt(s.substring(i*2,i*2+2),16); return r; } public byte[] toByteArray(){return b;} }',
'org/json/JSONObject.java':'''package org.json; import com.fasterxml.jackson.databind.*; public class JSONObject { JsonNode n; public JSONObject(String s)throws Exception{n=new ObjectMapper().readTree(s);} JSONObject(JsonNode j){n=j;} public JSONArray optJSONArray(String k){return n.has(k)?new JSONArray(n.get(k)):null;} public JSONObject optJSONObject(String k){return n.has(k)?new JSONObject(n.get(k)):null;} public int optInt(String k,int d){return n.path(k).asInt(d);} public String optString(String k,String d){return n.path(k).asText(d);} }''',
'org/json/JSONArray.java':'''package org.json; import com.fasterxml.jackson.databind.*; public class JSONArray { JsonNode n; JSONArray(JsonNode j){n=j;} public int length(){return n.size();} public JSONObject optJSONObject(int i){return new JSONObject(n.get(i));} }''',
'okhttp3/HttpUrl.java':'''package okhttp3; public class HttpUrl { public String data; public static HttpUrl parse(String s){return new HttpUrl();} public Builder newBuilder(){return new Builder();} public static class Builder { HttpUrl u=new HttpUrl(); public Builder addQueryParameter(String k,String v){if(k.equals("d"))u.data=v;return this;} public HttpUrl build(){return u;} } }''',
'okhttp3/Request.java':'''package okhttp3; public class Request { public HttpUrl u; public static class Builder { Request r=new Request(); public Builder url(String s){return this;} public Builder url(HttpUrl u){r.u=u;return this;} public Builder get(){return this;} public Request build(){return r;} } }''',
'okhttp3/ResponseBody.java':'''package okhttp3; public class ResponseBody { public String text; public String string(){return text;} }''',
'okhttp3/Response.java':'''package okhttp3; public class Response { public ResponseBody b=new ResponseBody(); public boolean isSuccessful(){return true;} public int code(){return 200;} public ResponseBody body(){return b;} public void close(){} }''',
'okhttp3/Call.java':r'''package okhttp3; public class Call { Request r; boolean cancelled; Call(Request r){this.r=r;} public void cancel(){cancelled=true;} public Response execute()throws Exception { if(cancelled)throw new java.io.IOException("cancelled"); OkHttpClient.calls++; String d=r.u.data, payload="FF".equals(d)?"00":"FE".equals(d)?"FE0000000000000000":d+"00"; Response out=new Response(); out.b.text="{\"responses\":[{\"s\":20,\"c\":97,\"m\":{\"hex\":\""+payload+"\"}}]}"; return out;} }''',
'okhttp3/OkHttpClient.java':'''package okhttp3; public class OkHttpClient { public static int calls; public Call newCall(Request r){return new Call(r);} public static class Builder { public Builder connectTimeout(long t,java.util.concurrent.TimeUnit u){return this;} public Builder readTimeout(long t,java.util.concurrent.TimeUnit u){return this;} public OkHttpClient build(){return new OkHttpClient();} } }''',
'com/here/oksse/ServerSentEvent.java':'''package com.here.oksse; import okhttp3.*; public class ServerSentEvent { public Listener listener; public boolean closed; public void close(){closed=true;} public void setTimeout(long t,java.util.concurrent.TimeUnit u){} public interface Listener { void onOpen(ServerSentEvent s,Response r); void onMessage(ServerSentEvent s,String id,String event,String message); void onComment(ServerSentEvent s,String c); boolean onRetryTime(ServerSentEvent s,long t); boolean onRetryError(ServerSentEvent s,Throwable t,Response r); void onClosed(ServerSentEvent s); Request onPreRetry(ServerSentEvent s,Request r); } }''',
'com/here/oksse/OkSse.java':'''package com.here.oksse; public class OkSse { public static int created; public static ServerSentEvent last; public ServerSentEvent newServerSentEvent(okhttp3.Request r,ServerSentEvent.Listener l){created++;last=new ServerSentEvent();last.listener=l;return last;} }''',
'Test.java':r'''import com.gargon.smarthome.sse.*;
import com.gargon.smarthome.model.*;
import com.gargon.smarthome.heatfloor.*;
import com.here.oksse.*;
public class Test {
 static int received,resyncs;
 static void check(boolean b){if(!b)throw new AssertionError();}
 static void event(String type,String payload){ServerSentEvent s=OkSse.last;s.listener.onMessage(s,"",type,payload);}
 static String row(int seq){return "[{\"bootId\":42,\"seq\":"+seq+",\"s\":20,\"d\":128,\"c\":97,\"m\":{\"hex\":\"00\"}}]";}
 public static void main(String[] args)throws Exception {
  HeatfloorBridgeClient h=new HeatfloorBridgeClient(new android.content.Context());
  h.loadSnapshot(); check(okhttp3.OkHttpClient.calls==12);
  h.loadSnapshot(); check(okhttp3.OkHttpClient.calls==14);
  SSESmarthomeClient a=new SSESmarthomeClient("http://bridge/events",30), b=new SSESmarthomeClient("http://bridge/events",30);
  check(OkSse.created==1);
  a.addListener(new SSESmarthomeMessageListener(){public void onMessage(SmarthomeMessage m){throw new RuntimeException("bad listener");}});
  b.addListener(new SSESmarthomeMessageListener(){public void onMessage(SmarthomeMessage m){received++;}});
  b.addStateListener(new SSESmarthomeClient.StateListener(){public void onState(boolean online,boolean resync){if(resync)resyncs++;}});
  event("RESET","{\"bootId\":42,\"sequence\":10}");check(resyncs==1);
  h.loadSnapshot();check(okhttp3.OkHttpClient.calls==26); // RESET invalidates warm program cache.
  event("DATA",row(9));event("DATA",row(11));event("DATA",row(11));check(received==1);
  event("DATA",row(13));check(received==2 && resyncs==2);
  event("SERVICE","{\"bootId\":42,\"sequence\":14,\"muted\":false}");check(resyncs==3);
  event("SERVICE","{\"bootId\":42,\"sequence\":14,\"muted\":false}");check(resyncs==3);
  a.close();check(!OkSse.last.closed);event("DATA",row(14));check(received==3);

  // Losses before ESP sequencing still require a fresh snapshot, once per change.
  int rs=resyncs;
  event("SERVICE","{\"bootId\":42,\"sequence\":14,\"avrQueueDrops\":1}");check(resyncs==rs+1);
  event("SERVICE","{\"bootId\":42,\"sequence\":14,\"avrQueueDrops\":1}");check(resyncs==rs+1);
  event("SERVICE","{\"bootId\":42,\"sequence\":14,\"avrQueueDrops\":2,\"muted\":true}");check(b.isTrafficMuted() && resyncs==rs+1);
  event("DATA",row(15));check(received==3);
  event("SERVICE","{\"bootId\":42,\"sequence\":15,\"avrQueueDrops\":2,\"muted\":false}");check(!b.isTrafficMuted() && resyncs==rs+2);
  // A delayed heartbeat must not move the monotonic bridge clock backwards.
  event("RESET","{\"bootId\":42,\"sequence\":15,\"uptimeMs\":1000}");
  android.os.SystemClock.now=3000;
  event("SERVICE","{\"bootId\":42,\"sequence\":15,\"uptimeMs\":1500}");
  event("DATA",row(16).replace("\"seq\":16", "\"uptimeMs\":1200,\"queuedMs\":0,\"seq\":16"));check(received==3);
  event("DATA",row(17).replace("\"seq\":17", "\"uptimeMs\":4000,\"queuedMs\":0,\"seq\":17"));check(received==4);
  event("DATA",row(18).replace("\"seq\":18", "\"uptimeMs\":4000,\"queuedMs\":2001,\"seq\":18"));check(received==4);
  // 32-bit uptime wrap remains fresh; a bridge restart discards the previous clock.
  event("RESET","{\"bootId\":42,\"sequence\":18,\"uptimeMs\":4294967280}");android.os.SystemClock.now+=32;
  event("DATA",row(19).replace("\"seq\":19", "\"uptimeMs\":16,\"seq\":19"));check(received==5);
  event("DATA",row(1).replace("\"bootId\":42", "\"bootId\":43,\"uptimeMs\":5"));check(received==6);
  b.close();check(OkSse.last.closed);
  java.lang.reflect.Method parse=HeatfloorBridgeClient.class.getDeclaredMethod("parseRequiredPayload",org.json.JSONObject.class,int.class);parse.setAccessible(true);
  org.json.JSONObject mixed=new org.json.JSONObject("{\"responses\":[{\"s\":21,\"c\":97,\"m\":{\"hex\":\"FE\"}},{\"s\":20,\"c\":97,\"m\":{\"hex\":\"F0\"}},{\"s\":20,\"c\":97,\"m\":{\"hex\":\"FE\"}}]}");
  check(((byte[])parse.invoke(h,mixed,254))[0]==(byte)254);
  System.out.println("PASS: Java 7 client compilation, shared SSE lifetime, duplicate/gap/AVR loss/maintenance detection, stale event rejection/clock wrap, listener isolation, program cache refresh, response filtering (mocked platform/transport)");
 }
}'''}
for name,text in files.items():
 f=w/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text(text)
base='app/src/main/java'
for name in ['sse/SSESmarthomeClient.java','sse/SSESmarthomeMessageListener.java','heatfloor/HeatfloorBridgeClient.java','heatfloor/HeatfloorSnapshot.java','heatfloor/HeatfloorCatalog.java','model/SmarthomeMessage.java','model/SmarthomeMessagePayload.java']:
 rel=Path('com/gargon/smarthome')/name;source=a.monitor/base/rel
 if not source.exists():source=a.original/base/rel
 target=w/rel;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
jars=list(a.jackson_dir.glob('jackson-*.jar'))
if len(jars)<3:raise SystemExit('Provide --jackson-dir containing jackson core, databind and annotations JARs')
cp=':'.join(map(str,jars));out=w/'classes';out.mkdir()
subprocess.run([str(a.javac),'-source','7','-target','7','-Xlint:-options','-cp',cp,'-d',str(out),*[str(f) for f in w.rglob('*.java')]],check=True)
subprocess.run([str(a.javac.with_name('java')),'-cp',str(out)+':'+cp,'Test'],check=True)
