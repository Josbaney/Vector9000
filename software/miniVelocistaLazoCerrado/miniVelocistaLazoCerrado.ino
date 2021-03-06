#include <Arduino.h>
//#include <EnableInterrupt.h>
#include "Vector9000.h"

#define PIN_BOTON 7

#define speed2ticks(a) ( (a*600) / (31.1*3.1415) ) //speed in m/s 
#define ticks2speed(a) ( (a/600) * (31.1*3.1415) )

#define DIFF_ENCODERS_RECT 13
#define TIME_MAX_IN_RECTA 600 //tiempo máximo que está el robot en recta antes de frena. En ms.
#define TIME_MAX_FRENADA 30 //tiempo maximo de duracion de frenada tras recta
#define TIME_MAX_PUENTE 100 //tiempo maximo de duracion de frenada tras recta
#define TICK_ENC_MAX_RECTA 500 //ticks máximos que dura la recta antes de frenar. 50ticks por vuelta de rueda.
#define TICK_ENC_MAX_FRENADA 100 //ticks máximos que dura la frenada

#define INTERVAL_RECT_TASK 100
#define MIN_VEL_EN_RECTA 10

/*int VEL_BASE_PUENTE=speed2ticks(1);
int VEL_BASE_RECTA=speed2ticks(1);
int VEL_BASE_CURVA=speed2ticks(1);
int VEL_BASE_FRENO=speed2ticks(1);
int VEL_BASE= speed2ticks(1);*/
//Vector9000 robot = Vector9000(0.047/20.0,4050.283/20.0,0);//(kp,kd, ki);
//Vector9000 robot = Vector9000(0.0012,150,0);
Vector9000 robot = Vector9000(0.0491,2950.283,0);//(kp,kd, ki);
int VEL_BASE_PUENTE=0;
int VEL_BASE_RECTA=102;//120
int VEL_BASE_CURVA=102;//110
int VEL_BASE_FRENO=-45;
int VEL_BASE=45;//105;

boolean schedulerVELBASEOn=false;
unsigned long nextTaskTime=4967295;
unsigned long nextEndDerTaskCount=4294967295;
unsigned long nextEndIzqTaskCount=4294967295;
void (*callback)(void);


boolean enRecta=false;
unsigned long nextCheckRectTask=0;
 long lastEncDer=0;
 long lastEncIzq=0;

#include "speedController.h"
/**
 * Control variables
 */
 extern int targetSpeedL;
 extern int targetSpeedR;

/**
 * Output variable. To be readed
 */
 extern long encoderCount;
 extern float curSpeedL;
 extern float curSpeedR;
 extern float accX;
 extern float decX;


/**
 * Tune variables
 */
// extern float ir_weight;
// extern float kpX, kdX;
// extern float kpW, kdW;//used in straight
// extern float kpW0, kdW0;//used in straight
// extern float kpWir, kdWir;//used with IR errors

void printTelemetria(float err){
    Serial.print("pT ");
    Serial.print(micros()/1000000.0);
    Serial.print("/");
    Serial.print(err);
    Serial.print("|");
    Serial.print(robot.readLine());
    Serial.print("|");
    Serial.print(robot._kp*1000);
    Serial.print(" ");
    Serial.print(robot._kd);
    Serial.print(" ");
    Serial.println(robot._ki);
}

bool boton_pulsado() {
  return !digitalRead(PIN_BOTON);
}

//volatile unsigned long cuentaEncoderIzquierdo = 0; 
//volatile unsigned long cuentaEncoderDerecho = 0;

volatile long count_enc_r;
volatile long count_enc_l;

void aumentarCuentaIzquierda()
{
  //cuentaEncoderIzquierdo++;
  static int8_t lookup_table[] = {0,0,0,-1,0,0,1,0,0,1,0,0,-1,0,0,0};
  static uint8_t enc2_val = 0;

    uint8_t v = (digitalRead(A0)<<1) | digitalRead(2);
    enc2_val = enc2_val << 2;
    enc2_val = enc2_val | (v & 0b11);

  count_enc_l = count_enc_l + lookup_table[enc2_val & 0b1111];

}

void aumentarCuentaDerecha()
{
  //cuentaEncoderDerecho++;


   static int8_t lookup_table[] = {0,0,0,-1,0,0,1,0,0,1,0,0,-1,0,0,0};
   static uint8_t enc1_val = 0;

   uint8_t v = (digitalRead(4)<<1) | digitalRead(3);
   enc1_val = enc1_val << 2;
   enc1_val = enc1_val | (v & 0b11);

   count_enc_r = count_enc_r - lookup_table[enc1_val & 0b1111];
}

void setup(){
    robot.config();
    Serial.begin(19200);
    pinMode(PIN_BOTON, INPUT_PULLUP);
    delay(20);
    
    robot.calibrateIR( 5, true );
    long millisini = millis();
    while(!boton_pulsado()){
      delay(10);
      if (millis() > millisini + 5000){
        robot.ledOn();
          VEL_BASE_PUENTE=speed2ticks(0.9);
         VEL_BASE_RECTA=speed2ticks(0.9);
         VEL_BASE_CURVA=speed2ticks(0.9);
         VEL_BASE_FRENO=speed2ticks(0.9);
         VEL_BASE= speed2ticks(0.93);
      }
    }
    while(boton_pulsado()) delay(10);
    
    attachInterrupt(digitalPinToInterrupt(Vector9000::ENC_DER_PIN), aumentarCuentaDerecha, CHANGE); 
    attachInterrupt(digitalPinToInterrupt(Vector9000::ENC_IZQ_PIN), aumentarCuentaIzquierda, CHANGE);

    delay(4960);
    resetSpeedProfile();
}



void inline activarCurvas(){
  VEL_BASE=VEL_BASE_CURVA;
  robot.config();
}
unsigned long contRecta=0;
void inline activarFreno(){
  enRecta=false;
  unsigned int longRecta= contRecta*1;
  if(longRecta>30)longRecta=30;
  VEL_BASE=VEL_BASE_FRENO-longRecta;
  robot.config();
  //Configurar duracion de frenada
  schedulerVELBASEOn=true;
  callback=&activarCurvas;
  nextTaskTime=millis()+TIME_MAX_FRENADA;
  nextEndDerTaskCount=count_enc_r+TICK_ENC_MAX_FRENADA;
  nextEndIzqTaskCount=count_enc_l+TICK_ENC_MAX_FRENADA;
}

void inline activarRecta(){
  enRecta=true;
  VEL_BASE= VEL_BASE_RECTA;
  //Configurar el punto de frenada
  //schedulerVELBASEOn=true;
  //callback=&activarFreno;
  //nextTaskTime=millis()+TIME_MAX_IN_RECTA;
  //nextEndDerTaskCount=cuentaEncoderDerecho+TICK_ENC_MAX_RECTA;
  //nextEndIzqTaskCount=cuentaEncoderIzquierdo+TICK_ENC_MAX_RECTA;
}

void inline activarPuente(){
  enRecta=false;
  VEL_BASE=VEL_BASE_PUENTE;
  robot.config();
  //Configurar duracion de frenada
  schedulerVELBASEOn=true;
  callback=&activarCurvas;
  nextTaskTime=millis()+TIME_MAX_PUENTE;
}

double posX=0;
double posY=0;
double ang =0;
long last_count_enc_r=0;
long last_count_enc_l=0;
#define DIAMETRO 30.4
#define N_MOTOR  10.0
#define RES_ENC  6.0
#define LEN 165
float Cm = (float(3.1416 * DIAMETRO))/(float(N_MOTOR * RES_ENC));


void inline calcularPosOdometria(){
  long enc1 = count_enc_r - last_count_enc_r;
  last_count_enc_r=count_enc_r;
  long enc2 = count_enc_l - last_count_enc_l;
  last_count_enc_l=count_enc_l;
  float avance1 = (float(enc1))*Cm;
  float avance2 = (float(enc2))*Cm;
  float varPosC = (avance1+avance2)/2;
  ang +=  (avance1-avance2)/LEN;
  posX +=varPosC*cos(ang);
  posY +=varPosC*sin(ang);
}

unsigned long nextSpeedProfile = 0;
unsigned long nextMainPID = 0;
unsigned long nextSendPos =0;
unsigned long nextCalcPos=0;
void loop() {
    if(boton_pulsado()) {
      robot.setSpeed(0,0);
      delay(1000);
      setup();
      return;
    }
//    if(schedulerVELBASEOn && (millis()>nextTaskTime)) {// || cuentaEncoderDerecho>nextEndDerTaskCount || cuentaEncoderIzquierdo>nextEndIzqTaskCount) ){ //planificador por tiempo para acelerar/frenar en recta
//      schedulerVELBASEOn=false;
//      callback();
//    }
      /*if(millis() > nextMainPID){
        nextMainPID=millis()+30;
        double errDif = robot.getErrorLine();//PID(err);
        //Serial.println(errDif);
        //printTelemetria(errDif);
        curSpeedR = VEL_BASE + errDif;
        curSpeedL = VEL_BASE - errDif;
      }
    
    if(millis() > nextSpeedProfile){
      nextSpeedProfile=millis()+10;
      speedProfile(NULL);
    }*/

    double errDif = robot.getErrorLine();//PID(err);
    //printTelemetria(errDif);
    robot.setSpeed( VEL_BASE - errDif, VEL_BASE + errDif );
    

//    if(millis()>nextCheckRectTask){
//      nextCheckRectTask=millis()+INTERVAL_RECT_TASK;
//       long velIzq=(count_enc_l-lastEncIzq);
//       long velDer=(count_enc_r-lastEncDer);
//      
//      long diferencia = (velIzq > velDer) ? (velIzq - velDer) : (velDer - velIzq);
//      if( diferencia < speed2ticks(0.05) && velIzq > speed2ticks(0.98)){
//        activarRecta();
//      }else if(enRecta && diferencia > DIFF_ENCODERS_RECT+DIFF_ENCODERS_RECT/3 ){
//        activarFreno();
//      }
//      if(enRecta){
//        robot.ledOn();
//        contRecta++;
//      }
//      else{
//        robot.ledOff();
//        contRecta=0;
//      }
//      if(velDer > 260 || velIzq >260){
//        activarPuente();
//      }*/
//      lastEncIzq = count_enc_l;
//      lastEncDer = count_enc_r;
//      //long cuenta = cuentaEncoderIzquierdo - cuentaEncoderDerecho;
//      //Serial.print(millis());Serial.print(" ");Serial.print(velDer);Serial.print("  ");Serial.print(velIzq);Serial.print("  "); Serial.print(diferencia);Serial.print("  "); Serial.print(enRecta*100);Serial.print("  "); Serial.println(cuenta);
//    }

    if(millis() > nextSendPos){
      nextSendPos = millis()+100;
      Serial.print(millis());Serial.print(" ");
      Serial.print(ang);Serial.print(" ");Serial.print(posX);Serial.print(" ");Serial.println(posY);
    }

    if(millis() > nextCalcPos){
      nextCalcPos = millis()+20;
      calcularPosOdometria();
    }

    
}

void loopL(){
    curSpeedR = -2 ;
    curSpeedL = -2;
    //Serial.println(robot.readRawErrLine() );   

    
    if(millis() > nextSpeedProfile){
      nextSpeedProfile=millis()+10;
      unsigned long iniTime = micros();
      speedProfile(NULL);
      Serial.println(micros() - iniTime);
    }
  
}



