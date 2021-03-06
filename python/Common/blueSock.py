from lightblue import *

class blueSock(object):
    def __init__(self, address=None, channel=0):
        self.socket = socket(proto=RFCOMM)
        self.alive = True
        self.clientAddress = None
        self.connection = -1
        self.address = address
        self.channel = channel

    def startService(self, advertiseName, port = "", channel = 0):
        print "Starting service '%s'..." %(advertiseName)
        self.socket.bind((port,channel))
        self.socket.listen(1)
        advertise(advertiseName, self.socket, RFCOMM)
        self.connection, self.clientAddress = self.socket.accept()
        print "Connected by ", self.clientAddress

    def connect(self, advertiseName=None):
        if (self.address == None and advertise != None):
            self.findService(advertiseName)
        print "Connecting to address (%s, %d)..." % (self.address, self.channel)
        self.socket.connect((self.address, self.channel))
        self.connection = self.socket
        
    def findService(self, advertiseName):
        print "Finding service '%s'..." % advertiseName
        while True:
            services = findservices(name=advertiseName)
            if (len(services) > 0):
                self.address = services[0][0]
                self.channel = services[0][1]
                print "Service found"
                break;

    def sendData(self, data):
        #print "(blue)Sending data ", data
        #print "(blue) sending to ", self.connection.getpeername()
        self.connection.send(data)

    def receiveData(self):
        #print "(blue)Receiving data..."
        return self.connection.recv(1024)

    def close(self):
        self.alive = False
        self.connection.close()
