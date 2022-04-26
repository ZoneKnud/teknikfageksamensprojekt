const int motorPin1 = 7;
const int motorPin2 = 6;
const int motorRead = A1;


void setup() {
  Serial.begin(9600);
  Serial.println("Tændt");
  // put your setup code here, to run once:
  pinMode(motorPin1, OUTPUT);
  pinMode(motorPin2, OUTPUT);

  digitalWrite(motorPin1, LOW);
  digitalWrite(motorPin2, LOW);
}

void loop() {
  Serial.println(analogRead(motorRead));          // debug value
  giveFood();
  delay(10000);

}

void giveFood() {
  Serial.println("Feed me");
  digitalWrite(motorPin2, HIGH);
  delay(100);
  while(true) {
    Serial.print("reading..");
    Serial.println(analogRead(motorRead));
    if (analogRead(motorRead) > 100) {
      digitalWrite(motorPin2, LOW);
      delay(200);
      digitalWrite(motorPin1, HIGH);
      delay(300);
      break;
    }
  }
  
  while(true) {
    if (analogRead(motorRead) > 100) {
      digitalWrite(motorPin1, LOW);
      break;
    }
  }

}
