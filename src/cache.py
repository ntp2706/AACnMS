import os
import cv2
import dlib
import numpy as np
import faiss
from glob import glob
from concurrent.futures import ThreadPoolExecutor
import pickle

detector = dlib.get_frontal_face_detector()
sp = dlib.shape_predictor("model/shape_predictor_68_face_landmarks.dat")
face_rec_model = dlib.face_recognition_model_v1("model/dlib_face_recognition_resnet_model_v1.dat")

def get_face_descriptor(image_path):
    try:
        
        img = cv2.imread(image_path)     
        scale = 0.5
        width = int(img.shape[1] * scale)
        height = int(img.shape[0] * scale)
            
        img = cv2.resize(img, (width, height))
        rgb_img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
        dets = detector(rgb_img)
        
        print(f"Number of faces detected: {len(dets)}")
        
        if len(dets) == 0:
            print(f"No face found in image")
            return None
        
        shape = sp(rgb_img, dets[0])
        descriptor = face_rec_model.compute_face_descriptor(rgb_img, shape)
        return np.array(descriptor, dtype=np.float32)
    except Exception as e:
        print(f"Error processing image: {str(e)}")
        return None

def index_database(database_path, cache_file="face_descriptors_cache.pkl"):
    try:
        print("\n" + "=" * 50)
        print("Starting database indexing...")
        print("=" * 50 + "\n")
        
        if not os.path.exists(database_path):
            print(f"Waiting for database directory: {database_path}")
            return None, None, None
            
        current_files = set()
        current_folders = {}
        for folder in os.listdir(database_path):
            folder_path = os.path.join(database_path, folder)
            if os.path.isdir(folder_path):
                img_files = glob(os.path.join(folder_path, "sample*.jpg"))
                if img_files:
                    current_folders[folder] = img_files
                    current_files.update(img_files)

        index = faiss.IndexFlatL2(128)
        image_paths = []
        labels = []
        descriptors = []
        
        processed_files = set()
        invalid_files = set()
        if os.path.exists(cache_file):
            try:
                with open(cache_file, 'rb') as f:
                    cache_data = pickle.load(f)
                    
                invalid_files = set(cache_data.get('invalid_files', []))
                
                invalid_files = {f for f in invalid_files if f in current_files}
                
                for i, path in enumerate(cache_data['image_paths']):
                    if path in current_files:
                        descriptors.append(cache_data['descriptors'][i])
                        image_paths.append(path)
                        labels.append(cache_data['labels'][i])
                        processed_files.add(path)
                        print(f"Reusing cached data for {path}")
            except Exception as e:
                print(f"Error reading cache: {str(e)}")
                invalid_files = set()

        new_files = current_files - processed_files - invalid_files
        if new_files:
            print("\nProcessing new files:")
            for img_path in new_files:
                folder = os.path.basename(os.path.dirname(img_path))
                print(f"\nProcessing new image: {img_path}")
                descriptor = get_face_descriptor(img_path)
                if descriptor is not None:
                    descriptors.append(descriptor)
                    image_paths.append(img_path)
                    labels.append(folder)
                    print(f"Successfully processed")
                else:
                    print(f"No face found - marking as invalid")
                    invalid_files.add(img_path)
        else:
            print("\nNo new files to process")

        if not descriptors:
            if os.path.exists(cache_file):
                try:
                    os.remove(cache_file)
                    print(f"Deleted cache file: {cache_file}")
                except Exception as e:
                    print(f"Error deleting cache file: {str(e)}")
            raise Exception("No faces found in any images")
            
        print("\n" + "=" * 50)
        print(f"Successfully processed {len(descriptors)} images total")
        print(f"New files processed: {len(new_files)}")
        print(f"Invalid files (no faces): {len(invalid_files)}")
        print("=" * 50 + "\n")
        
        descriptors_array = np.array(descriptors)
        index.add(descriptors_array)
        
        cache_data = {
            'image_paths': image_paths,
            'labels': labels,
            'descriptors': descriptors,
            'invalid_files': list(invalid_files)
        }
        try:
            with open(cache_file, 'wb') as f:
                pickle.dump(cache_data, f)
                print("Data cached successfully")
        except Exception as e:
            print(f"Error saving cache: {str(e)}")
        
        return index, image_paths, labels
        
    except Exception as e:
        print(f"Error processing database: {str(e)}")
        raise

if __name__ == "__main__":
    database_path = os.path.join(os.path.dirname(__file__), "database")
    print(f"Processing database at: {database_path}")
    
    index, image_paths, labels = index_database(database_path)
    if index is not None:
        print("Processing completed and cached")
    else:
        print("No data found to process")
