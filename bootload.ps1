$port = new-Object System.IO.Ports.SerialPort COM7,1200,None,8,one
$port.open()
$port.Close()