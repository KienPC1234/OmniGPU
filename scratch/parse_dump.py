from minidump.minidumpfile import MinidumpFile

def parse():
    dmp_path = r'C:\Users\kien\.gemini\antigravity-ide\brain\35b8369f-3a24-4fde-a8d2-3cb0026e8b46\scratch\llama-cli.exe.7348.dmp'
    dmp = MinidumpFile.parse(dmp_path)

    if dmp.exception and len(dmp.exception.exception_records) > 0:
        er = dmp.exception.exception_records[0].ExceptionRecord
        print("Type of ExceptionCode:", type(er.ExceptionCode))
        for attr in dir(er.ExceptionCode):
            print(f"ExceptionCode attr: {attr} -> {getattr(er.ExceptionCode, attr)}")
            
        print("Type of er:", type(er))
        for attr in dir(er):
            if not attr.startswith('_'):
                print(f"er attr: {attr} -> {getattr(er, attr)}")

if __name__ == '__main__':
    parse()
